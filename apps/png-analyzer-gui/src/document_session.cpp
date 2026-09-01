// WP-5U15: document lifecycle moved verbatim from the facade. Every worker
// parent, connection type, finished -> deleteLater chain, generation gate and
// publication order is preserved; only the owning object changes.

#include "document_session.h"

#include <pnga/png-reconstruction/scanline_layout.h>

#include <QFileInfo>
#include <QMetaObject>

#include <filesystem>
#include <system_error>
#include <utility>

namespace {

std::filesystem::path filesystemPath(const QString& path) {
#if defined(Q_OS_WIN)
  // std::filesystem::path's narrow constructor follows the active Windows
  // code page, so build the path from the UTF-16 native string instead.
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

}  // namespace

DocumentSession::DocumentSession(QObject* parent) : QObject(parent) {
  query_bridge_ = new QueryStatusBridge(this);
  connect(query_bridge_, &QueryStatusBridge::rowStatus, this,
          &DocumentSession::rowQueryStatus);
}

DocumentSession::~DocumentSession() {
  // ~QObject deletes the child worker QThreads, and destroying a still
  // running QThread is fatal. The facade may reach this destructor while an
  // in-flight (or a replaced, not yet deleteLater'd) worker is running, and
  // unlike the previous facade-owned children they are no longer shielded by
  // the whole widget teardown. Every result is generation-gated, so joining
  // the threads here is safe and preserves observable behavior.
  const auto workers = findChildren<QThread*>();
  for (QThread* worker : workers) {
    if (worker->isRunning()) {
      worker->wait();
    }
  }
}

bool DocumentSession::replace(const QString& path) {
  std::unique_ptr<pnga::io::IByteSource> opened;
  const std::error_code ec =
      pnga::io::open_mapped_file(filesystemPath(path), opened);
  if (ec) {
    return false;
  }
  // Shared ownership so an in-flight worker keeps its source alive even when
  // a newer file replaces source_ (virtual dtor makes this safe).
  auto source = std::shared_ptr<pnga::io::IByteSource>(opened.release());
  ++generation_;
  source_ = std::move(source);
  index_ = pnga::png_format::index_chunks(*source_);
  stage_set_.reset();
  validation_report_ = {};
  decode_result_ = {};
  chunk_detail_ = {};
  query_.reset();
  decode_worker_ = nullptr;
  stage_worker_ = nullptr;
  validation_worker_ = nullptr;
  chunk_detail_worker_ = nullptr;
  current_file_path_ = QFileInfo(path).absoluteFilePath();
  emit replaced(generation_);
  return true;
}

void DocumentSession::close() {
  ++generation_;
  source_.reset();
  index_ = {};
  stage_set_.reset();
  validation_report_ = {};
  decode_result_ = {};
  chunk_detail_ = {};
  query_.reset();
  decode_worker_ = nullptr;
  stage_worker_ = nullptr;
  validation_worker_ = nullptr;
  chunk_detail_worker_ = nullptr;
  current_file_path_.clear();
  emit closed(generation_);
}

void DocumentSession::startPrimaryWorkers() {
  startValidation();
  startDecode();
  startStageAnalysis();
}

void DocumentSession::openQueryCoordinator(
    const pnga::png_reconstruction::ImageHeader& header) {
  query_.reset();
  query_ = std::make_unique<pnga::analysis_engine::QueryCoordinator>(
      /*worker_count=*/2, /*replay_budget_bytes=*/1ull << 26);
  const std::shared_ptr<const pnga::io::IByteSource> shared = source_;
  if (!query_->open(shared, header, /*anchor_interval_bytes=*/16384)) {
    query_.reset();
    return;
  }
  // Bridge the worker-thread status callback onto the GUI thread.
  query_->setStatusCallback(
      [bridge = query_bridge_](std::uint64_t row,
                               pnga::analysis_engine::QueryStatus status) {
        QMetaObject::invokeMethod(
            bridge, [bridge, row, status] {
              emit bridge->rowStatus(row, static_cast<int>(status));
            },
            Qt::QueuedConnection);
      });
}

void DocumentSession::requestChunkDetail(
    const pnga::png_format::ChunkNode& node,
    std::uint64_t selection_serial) {
  if (source_ == nullptr) {
    return;
  }
  auto* detail_worker = new ChunkDetailWorker(
      generation_, selection_serial, source_, node, this);
  chunk_detail_worker_ = detail_worker;
  requested_detail_serial_ = selection_serial;
  connect(detail_worker, &ChunkDetailWorker::detailDone, this,
          &DocumentSession::onChunkDetailDone);
  connect(detail_worker, &QThread::finished, detail_worker,
          &QObject::deleteLater);
  connect(detail_worker, &QThread::finished, this,
          [this, detail_worker] {
            if (chunk_detail_worker_ == detail_worker) {
              chunk_detail_worker_ = nullptr;
            }
          });
  detail_worker->start();
}

std::uint64_t DocumentSession::generation() const noexcept {
  return generation_;
}

bool DocumentSession::hasDocument() const noexcept {
  return source_ != nullptr;
}

const QString& DocumentSession::currentFilePath() const noexcept {
  return current_file_path_;
}

const pnga::png_format::ChunkIndex& DocumentSession::index() const noexcept {
  return index_;
}

std::shared_ptr<const pnga::io::IByteSource> DocumentSession::source() const {
  return source_;
}

const pnga::backend_libpng::ReferenceResult&
DocumentSession::decodeResult() const noexcept {
  return decode_result_;
}

std::shared_ptr<const pnga::analysis_engine::StageSet>
DocumentSession::stageSet() const {
  return stage_set_;
}

const pnga::analysis_engine::DocumentValidationReport&
DocumentSession::validationReport() const noexcept {
  return validation_report_;
}

const pnga::png_format::ChunkDetail& DocumentSession::chunkDetail()
    const noexcept {
  return chunk_detail_;
}

pnga::analysis_engine::QueryCoordinator*
DocumentSession::queryCoordinator() noexcept {
  return query_.get();
}

void DocumentSession::startDecode() {
  if (decode_worker_ != nullptr) {
    // A previous decode may still be running; its result will be ignored
    // because its generation is stale. Drop the reference now.
    decode_worker_ = nullptr;
  }
  auto* worker = new DecodeWorker(generation_, source_, this);
  decode_worker_ = worker;
  connect(worker, &DecodeWorker::decodeDone, this,
          &DocumentSession::onDecodeDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void DocumentSession::startStageAnalysis() {
  if (stage_worker_ != nullptr) {
    stage_worker_ = nullptr;  // in-flight stage result becomes stale
  }
  auto* worker = new StageWorker(generation_, source_, this);
  stage_worker_ = worker;
  connect(worker, &StageWorker::stageDone, this,
          &DocumentSession::onStageDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void DocumentSession::startValidation() {
  if (validation_worker_ != nullptr) {
    validation_worker_ = nullptr;
  }
  auto* worker = new ValidationWorker(generation_, source_, index_, this);
  validation_worker_ = worker;
  connect(worker, &ValidationWorker::validationDone, this,
          &DocumentSession::onValidationDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void DocumentSession::onDecodeDone(std::uint64_t generation) {
  if (generation != generation_ || decode_worker_ == nullptr) {
    return;  // stale decode; never overwrite the current document's image
  }
  decode_result_ = decode_worker_->result();
  decode_worker_ = nullptr;
  emit decodePublished(generation);
}

void DocumentSession::onStageDone(std::uint64_t generation) {
  if (generation != generation_ || stage_worker_ == nullptr) {
    return;  // stale stage analysis; never overwrite the current document
  }
  stage_set_ = stage_worker_->result();
  stage_worker_ = nullptr;
  openQueryCoordinator(stage_set_->header);
  emit stagesPublished(generation);
}

void DocumentSession::onValidationDone(std::uint64_t generation) {
  if (generation != generation_ || validation_worker_ == nullptr) {
    return;
  }
  validation_report_ = validation_worker_->result();
  validation_worker_ = nullptr;
  emit validationPublished(generation);
}

void DocumentSession::onChunkDetailDone(std::uint64_t generation,
                                        std::uint64_t selection_serial) {
  if (generation != generation_ || chunk_detail_worker_ == nullptr ||
      chunk_detail_worker_->selectionSerial() != selection_serial ||
      selection_serial != requested_detail_serial_) {
    return;
  }
  chunk_detail_ = chunk_detail_worker_->result();
  emit chunkDetailPublished(generation, selection_serial);
}
