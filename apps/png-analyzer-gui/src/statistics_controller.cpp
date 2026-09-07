// WP-602G: StatisticsController implementation. Everything the controller
// publishes is immutable and generation-gated: progress snapshots never
// clear verified rows (the view replaces rows atomically and selection is
// preserved by stable row id), occurrence queries run on a dedicated
// low-priority worker with cooperative cancellation and publish only an
// accepted ready selection for the current generation through the existing
// bus, and exports serialize through the sole shared serializer before the
// QSaveFile destination is opened so a failure can never overwrite the
// prior file.

#include "statistics_controller.h"

#include <pnga/deflate-index/block_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/statistics/serialization.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/statistics_inspector.h>
#include <pnga/ui/qt/statistics_table_model.h>

#include <QFileDialog>
#include <QIODevice>
#include <QSaveFile>
#include <QTabWidget>

#include <optional>
#include <utility>

namespace {

// The statistics panel's bus origin (chunk=1, image=2, hex=3 exist).
constexpr int kStatisticsPanelOrigin = 4;

// The frozen declared working-memory cap (ruling R6).
constexpr std::uint64_t kMaxWorkingBytes = 64ull << 20;

// The bounded fast-index output budget of the existing Deep Trace index
// capability; occurrence navigation never exceeds it.
constexpr std::uint64_t kOccurrenceIndexOutputBytes = 1ull << 26;

// Retained-block bounds of the occurrence pre-scan: the frozen statistics
// sample budget for the block count, and half of the declared 64 MiB
// working memory for the retained block vector (reallocation peaks
// included). The pre-scan's only unbounded allocation stays capped.
constexpr std::uint64_t kOccurrenceIndexMaxBlocks = 1ull << 20;
constexpr std::uint64_t kOccurrenceIndexRetainedBytes = 32ull << 20;

// A section carries exportable evidence when it is neither unavailable,
// cancelled nor an error — the shared "usable" semantics of the CLI.
bool section_usable(const pnga::statistics::SectionState& state) noexcept {
  switch (state.status) {
    case pnga::statistics::SectionStatus::kUnavailable:
    case pnga::statistics::SectionStatus::kCancelled:
    case pnga::statistics::SectionStatus::kError:
      return false;
    default:
      return true;
  }
}

bool any_section_usable(
    const pnga::statistics::StatisticsSnapshot& snapshot) noexcept {
  const auto& s = snapshot;
  return section_usable(s.overview.state) || section_usable(s.chunks.state) ||
         section_usable(s.filters.state) || section_usable(s.blocks.state) ||
         section_usable(s.tokens.state) || section_usable(s.lengths.state) ||
         section_usable(s.distances.state);
}

// Adapts the virtual IDAT stream to IByteSource without concatenating the
// payloads. Borrowed: the stream and the file source must outlive the
// adapter, guaranteed by the occurrence query scope.
class VirtualIdatByteSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatByteSource(const pnga::png_format::VirtualIDATStream& stream,
                        const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const pnga::png_format::VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

}  // namespace

StatisticsOccurrenceWorker::StatisticsOccurrenceWorker(
    std::shared_ptr<const pnga::io::IByteSource> source,
    pnga::png_format::ChunkIndex chunks,
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages,
    pnga::analysis_engine::StatisticsNavigationRequest request,
    int request_serial, QObject* parent)
    : QThread(parent),
      source_(std::move(source)),
      chunks_(std::move(chunks)),
      stages_(std::move(stages)),
      request_(std::move(request)),
      request_serial_(request_serial),
      cancellation_(std::make_shared<pnga::analysis_engine::CancellationToken>()) {
  qRegisterMetaType<pnga::analysis_engine::StatisticsOccurrenceResult>(
      "pnga::analysis_engine::StatisticsOccurrenceResult");
}

void StatisticsOccurrenceWorker::cancel() noexcept {
  cancellation_->request_cancel();
}

void StatisticsOccurrenceWorker::run() {
  pnga::deflate_index::BlockIndexResult block_index;
  const bool needs_blocks =
      request_.domain !=
      pnga::analysis_engine::StatisticsBucketDomain::kChunkType;
  if (needs_blocks) {
    // Bounded, cancelable pre-scan. The index consumes at most the same
    // 8 MiB input window the query replay is allowed to search, so the
    // total read work per request stays at one index pass plus one replay
    // over that window — there is no retry loop and no full-file scan
    // before the query's bounded token counting starts. A verified prefix
    // (typed stop on a limit or cancellation) is consumed honestly by the
    // query; answers that would need unindexed blocks stay Partial.
    const pnga::png_format::VirtualIDATStream stream(chunks_);
    VirtualIdatByteSource logical(stream, *source_);
    pnga::deflate_index::BlockScanLimits limits;
    limits.max_input_bytes = request_.max_input_bytes;
    limits.max_output_bytes = kOccurrenceIndexOutputBytes;
    limits.max_blocks = kOccurrenceIndexMaxBlocks;
    limits.max_retained_bytes = kOccurrenceIndexRetainedBytes;
    block_index = pnga::deflate_index::index_blocks_bounded(
                      logical, limits,
                      [cancellation = cancellation_] {
                        return cancellation->cancelled();
                      })
                      .index;
  }
  pnga::analysis_engine::StatisticsOccurrenceResult result =
      pnga::analysis_engine::query_statistics_occurrence(
          *source_, chunks_, stages_.get(),
          needs_blocks ? &block_index : nullptr, request_,
          cancellation_.get());
  emit occurrenceDone(request_.generation, request_serial_, std::move(result));
}

StatisticsController::StatisticsController(MainWindowWidgets widgets,
                                           DocumentSession& session,
                                           QObject* parent)
    : QObject(parent), w_(widgets), session_(session) {
  connect(&session_, &DocumentSession::replaced, this,
          &StatisticsController::onDocumentReplaced);
  connect(&session_, &DocumentSession::closed, this,
          &StatisticsController::onDocumentClosed);
  connect(&session_, &DocumentSession::stagesPublished, this,
          &StatisticsController::onStagesPublished);
  connect(&session_, &DocumentSession::statisticsProgress, this,
          &StatisticsController::onStatisticsProgress);
  connect(&session_, &DocumentSession::statisticsFinished, this,
          &StatisticsController::onStatisticsFinished);
  if (w_.statistics_inspector != nullptr) {
    connect(w_.statistics_inspector,
            &pnga::ui::qt::StatisticsInspector::refreshRequested, this,
            &StatisticsController::onRefreshRequested);
    connect(w_.statistics_inspector,
            &pnga::ui::qt::StatisticsInspector::cancelRequested, this,
            &StatisticsController::onCancelRequested);
    connect(w_.statistics_inspector,
            &pnga::ui::qt::StatisticsInspector::exportRequested, this,
            &StatisticsController::onExportRequested);
    connect(w_.statistics_inspector,
            &pnga::ui::qt::StatisticsInspector::occurrenceRequested, this,
            &StatisticsController::onOccurrenceRequested);
    if (w_.inspector_tabs != nullptr) {
      // Collection starts only on the first Statistics-tab activation (never
      // on file open or hover): the tab switch is the lazy trigger.
      connect(w_.inspector_tabs, &QTabWidget::currentChanged, this,
              [this](int index) {
                if (w_.inspector_tabs != nullptr && index >= 0 &&
                    w_.inspector_tabs->widget(index) ==
                        w_.statistics_inspector) {
                  onStatisticsTabActivated();
                }
              });
    }
  }
}

StatisticsController::~StatisticsController() {
  // DocumentSession precedent (document_session.cpp:139-151): destroying a
  // still-running QThread is fatal. The controller is destroyed before the
  // session, so it joins its own occurrence workers here. The scan is
  // bounded (4,096 tokens / 8 MiB input / bounded retained blocks), so
  // after the cooperative cancel every join terminates promptly.
  // Superseded workers stay children until their deleteLater runs, so all
  // of them are joined.
  if (occurrence_worker_ != nullptr) {
    occurrence_worker_->cancel();
  }
  const auto workers = findChildren<StatisticsOccurrenceWorker*>();
  for (StatisticsOccurrenceWorker* worker : workers) {
    if (worker->isRunning()) {
      worker->wait();
    }
  }
  occurrence_worker_ = nullptr;
}

void StatisticsController::onDocumentReplaced(std::uint64_t /*generation*/) {
  if (w_.statistics_inspector != nullptr) {
    w_.statistics_inspector->clear();
  }
  result_.reset();
  last_progress_.reset();
  result_generation_ = 0;
  if (occurrence_worker_ != nullptr) {
    occurrence_worker_->cancel();
    occurrence_worker_ = nullptr;
  }
}

void StatisticsController::onDocumentClosed(std::uint64_t /*generation*/) {
  onDocumentReplaced(0);
}

void StatisticsController::onStagesPublished(std::uint64_t generation) {
  if (generation != session_.generation()) {
    return;
  }
  // When the Statistics page is already selected while a document opens,
  // the lazy request is armed here once the stages exist; selecting another
  // page (the file-open default) must start nothing.
  if (w_.inspector_tabs == nullptr || w_.statistics_inspector == nullptr ||
      w_.inspector_tabs->currentWidget() != w_.statistics_inspector) {
    return;
  }
  onStatisticsTabActivated();
}

void StatisticsController::onStatisticsTabActivated() {
  if (!session_.hasDocument()) {
    return;
  }
  // One lazy request per document generation: after a final result for the
  // current generation published, re-activation must not re-request.
  if (result_generation_ == session_.generation()) {
    return;
  }
  session_.requestStatistics();
}

void StatisticsController::onRefreshRequested() {
  // A new generation-scoped request for the current document; the session
  // never duplicates a worker while one runs.
  session_.requestStatistics();
}

void StatisticsController::onCancelRequested() {
  session_.cancelStatistics();
}

void StatisticsController::onExportRequested(int format) {
  exportStatistics(format);
}

void StatisticsController::onStatisticsProgress(
    std::uint64_t generation,
    std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
        result) {
  if (result == nullptr || generation != session_.generation() ||
      result->generation != generation) {
    return;  // stale progress never overwrites the current document's view
  }
  last_progress_ = result;
  publishResult(generation, *result);
  if (w_.statistics_inspector != nullptr) {
    w_.statistics_inspector->setProgress(
        QStringLiteral("Collecting statistics…"));
  }
}

void StatisticsController::onStatisticsFinished(
    std::uint64_t generation,
    std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
        result) {
  if (result == nullptr || generation != session_.generation() ||
      result->generation != generation) {
    return;  // stale results never overwrite the current document's view
  }
  result_ = result;
  result_generation_ = generation;
  publishResult(generation, *result);
  if (w_.statistics_inspector != nullptr) {
    // The partial outcome is labeled explicitly; verified rows stay visible.
    w_.statistics_inspector->setProgress(
        result->snapshot.complete()
            ? QStringLiteral("Statistics ready.")
            : QStringLiteral(
                  "Partial statistics — verified sections retained."));
  }
}

void StatisticsController::onOccurrenceRequested(
    pnga::analysis_engine::StatisticsNavigationRequest request) {
  if (!session_.hasDocument() || session_.stageSet() == nullptr) {
    return;
  }
  // One bounded occurrence query at a time; a newer request supersedes the
  // previous one, whose result then publishes nothing.
  if (occurrence_worker_ != nullptr) {
    occurrence_worker_->cancel();
    occurrence_worker_ = nullptr;
  }
  request.generation = session_.generation();
  const int serial = next_occurrence_serial_++;
  active_occurrence_serial_ = serial;
  auto* worker = new StatisticsOccurrenceWorker(
      session_.source(), session_.index(), session_.stageSet(), request,
      serial, this);
  occurrence_worker_ = worker;
  connect(worker, &StatisticsOccurrenceWorker::occurrenceDone, this,
          &StatisticsController::onOccurrenceDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  connect(worker, &QThread::finished, this, [this, worker] {
    // Identity-checked clear (DocumentSession precedent): the finish of a
    // superseded worker must not clear the pointer of a newer worker that
    // is still running, so at most one scan exists and the newest scan
    // stays cancelable.
    if (occurrence_worker_ == worker) {
      occurrence_worker_ = nullptr;
    }
  });
  worker->start();
}

void StatisticsController::onOccurrenceDone(
    std::uint64_t generation, int request_serial,
    pnga::analysis_engine::StatisticsOccurrenceResult result) {
  // Existing loop guards: a superseded serial, an older generation and a
  // non-ready outcome publish nothing through the bus, exactly once.
  if (request_serial != active_occurrence_serial_ ||
      generation != session_.generation() ||
      result.status != pnga::analysis_engine::OccurrenceStatus::kReady) {
    return;
  }
  if (w_.bus == nullptr) {
    return;
  }
  w_.bus->publish(kStatisticsPanelOrigin, generation, result.selection);
}

void StatisticsController::publishResult(
    std::uint64_t generation,
    const pnga::analysis_engine::StatisticsCollectionResult& collected) {
  if (w_.statistics_inspector == nullptr) {
    return;
  }
  // Snapshot-to-row projection into a fresh immutable view; the inspector
  // applies it atomically, preserving verified rows and the stable-id
  // selection across progress publications.
  auto view = std::make_shared<const pnga::analysis_engine::StatisticsView>(
      pnga::analysis_engine::build_statistics_view(generation,
                                                   collected.snapshot));
  w_.statistics_inspector->setView(std::move(view));
}

void StatisticsController::exportStatistics(int format) {
  if (w_.statistics_inspector == nullptr) {
    return;
  }
  // A gated export click must never be a silent no-op: the buttons enable on
  // the first verified section (mid-collection), so explain why the save
  // dialog did not open.
  // Mid-collection export: the latest progress result carries the verified
  // prefix and its own document identity; the snapshot's statuses label it
  // partial honestly (R13). With nothing collected yet, explain the refusal.
  // The dialog runs a nested event loop: progress can replace the controller's
  // snapshot while it is open. Keep ownership of the selected export.
  const auto exportable = result_ != nullptr ? result_ : last_progress_;
  const bool partial_export = exportable != nullptr && result_ == nullptr;
  if (exportable == nullptr) {
    w_.statistics_inspector->setProgress(QStringLiteral(
        "Export unavailable — statistics collection is still running."));
    return;
  }
  if (!any_section_usable(exportable->snapshot)) {
    w_.statistics_inspector->setProgress(QStringLiteral(
        "Export unavailable — no verified statistics section."));
    return;
  }
  const QString path = save_path_ ? save_path_() : [&]() {
    const bool json = format == static_cast<int>(
                                  pnga::ui::qt::StatisticsExportFormat::kJson);
    // Match File -> Open: development app bundles can fail to present the
    // native macOS panel. Set the Qt dialog option before other properties.
    QFileDialog dialog(w_.statistics_inspector);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowTitle(QStringLiteral("Export statistics"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    const QString suffix = json ? QStringLiteral("json") : QStringLiteral("csv");
    dialog.setDefaultSuffix(suffix);
    dialog.setNameFilter(QStringLiteral("Statistics (*.%1)").arg(suffix));
    dialog.selectFile(QStringLiteral("statistics.%1").arg(suffix));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
      return QString();
    }
    return dialog.selectedFiles().front();
  }();
  if (path.isEmpty()) {
    return;
  }
  const bool json =
      format == static_cast<int>(pnga::ui::qt::StatisticsExportFormat::kJson);
  // Serialize through the sole shared serializer BEFORE opening the
  // destination; no custom Statistics serialization path exists here.
  const auto serialized =
      json ? pnga::statistics::serialize_statistics_json(exportable->document,
                                                        exportable->snapshot)
           : pnga::statistics::serialize_statistics_csv(exportable->document,
                                                        exportable->snapshot);
  if (!serialized.success) {
    w_.statistics_inspector->setProgress(
        QStringLiteral("Export failed: %1")
            .arg(QString::fromStdString(serialized.error)));
    return;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    w_.statistics_inspector->setProgress(
        QStringLiteral("Export failed: cannot write %1").arg(path));
    return;
  }
  const qint64 expected = static_cast<qint64>(serialized.bytes.size());
  if (file.write(serialized.bytes.data(), expected) != expected ||
      !file.commit()) {
    file.cancelWriting();
    w_.statistics_inspector->setProgress(
        QStringLiteral("Export failed: cannot write %1").arg(path));
    return;
  }
  // Partial output remains exportable and is labeled explicitly; a
  // mid-collection export is partial by definition and labeled as such.
  const QString target = json ? QStringLiteral("JSON") : QStringLiteral("CSV");
  const bool partial_output = partial_export ||
                              !exportable->snapshot.complete();
  w_.statistics_inspector->setProgress(
      partial_output
          ? QStringLiteral("Exported %1 (partial): %2 — collection is still "
                           "running.").arg(target, path)
          : QStringLiteral("Exported %1: %2").arg(target, path));
}

void StatisticsController::setSavePathCallback(
    std::function<QString()> callback) {
  save_path_ = std::move(callback);
}
