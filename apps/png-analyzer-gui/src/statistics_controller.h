#ifndef PNG_ANALYZER_GUI_STATISTICS_CONTROLLER_H
#define PNG_ANALYZER_GUI_STATISTICS_CONTROLLER_H

// WP-602G: lazy Statistics orchestration. The controller owns the lazy start
// (first Statistics-tab activation, Refresh, Export), applies immutable
// views on the GUI thread from generation-gated session results, keeps
// verified rows across progress publications, runs bounded occurrence
// queries on background work and publishes accepted selections through the
// existing SelectionBus, and exports through the shared Qt-free serializer
// with QSaveFile atomic replacement (a serialization or write failure never
// overwrites the target). It owns no parsing, decoding or serialization
// logic.

#include "document_session.h"
#include "main_window_ui.h"

#include <pnga/analysis-engine/statistics_collector.h>
#include <pnga/analysis-engine/statistics_occurrence_query.h>
#include <pnga/analysis-engine/statistics_view.h>

#include <QObject>
#include <QString>
#include <QThread>

#include <cstdint>
#include <functional>
#include <memory>

Q_DECLARE_METATYPE(pnga::analysis_engine::StatisticsOccurrenceResult)

// One bounded occurrence query on a background thread. Direct chunk domains
// resolve from the cached chunk index; the other domains index the virtual
// IDAT stream first (bounded fast-index budget, never a payload copy). A
// newer request supersedes the previous one through cooperative
// cancellation, and the result echoes the request serial and generation so
// the controller can drop stale outcomes.
class StatisticsOccurrenceWorker final : public QThread {
  Q_OBJECT
 public:
  StatisticsOccurrenceWorker(
      std::shared_ptr<const pnga::io::IByteSource> source,
      pnga::png_format::ChunkIndex chunks,
      std::shared_ptr<const pnga::analysis_engine::StageSet> stages,
      pnga::analysis_engine::StatisticsNavigationRequest request,
      int request_serial, QObject* parent = nullptr);

  void cancel() noexcept;

 signals:
  void occurrenceDone(std::uint64_t generation, int request_serial,
                      pnga::analysis_engine::StatisticsOccurrenceResult result);

 protected:
  void run() override;

 private:
  std::shared_ptr<const pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex chunks_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stages_;
  pnga::analysis_engine::StatisticsNavigationRequest request_;
  int request_serial_ = 0;
  std::shared_ptr<pnga::analysis_engine::CancellationToken> cancellation_;
};

class StatisticsController final : public QObject {
  Q_OBJECT
 public:
  StatisticsController(MainWindowWidgets widgets, DocumentSession& session,
                       QObject* parent = nullptr);
  ~StatisticsController() override;

  // Session lifecycle entry points: clear the stale view, reset lazy state
  // and drop in-flight occurrence work. Collection itself never starts here.
  void onDocumentReplaced(std::uint64_t generation);
  void onDocumentClosed(std::uint64_t generation);

  // A pending statistics request (recorded before the stages existed) starts
  // through the session; the controller only re-arms the lazy state.
  void onStagesPublished(std::uint64_t generation);

  // Test seam: a deterministic save path for exports. Empty asks the user
  // through the default file dialog.
  void setSavePathCallback(std::function<QString()> callback);

 private slots:
  void onStatisticsTabActivated();
  void onRefreshRequested();
  void onCancelRequested();
  void onExportRequested(int format);
  void onOccurrenceRequested(
      pnga::analysis_engine::StatisticsNavigationRequest request);
  void onStatisticsProgress(
      std::uint64_t generation,
      std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
          result);
  void onStatisticsFinished(
      std::uint64_t generation,
      std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
          result);
  void onOccurrenceDone(std::uint64_t generation, int request_serial,
                        pnga::analysis_engine::StatisticsOccurrenceResult result);

 private:
  void publishResult(std::uint64_t generation,
                     const pnga::analysis_engine::StatisticsCollectionResult&
                         collected);
  void exportStatistics(int format);

  MainWindowWidgets w_;
  DocumentSession& session_;
  std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
      result_;
  // Generation of the last accepted final result; tab activation only
  // re-requests when this differs from the current document generation.
  std::uint64_t result_generation_ = 0;
  StatisticsOccurrenceWorker* occurrence_worker_ = nullptr;
  int active_occurrence_serial_ = 0;
  int next_occurrence_serial_ = 1;
  std::function<QString()> save_path_;
};

#endif  // PNG_ANALYZER_GUI_STATISTICS_CONTROLLER_H
