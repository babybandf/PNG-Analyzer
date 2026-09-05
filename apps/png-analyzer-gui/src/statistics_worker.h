#ifndef PNG_ANALYZER_GUI_STATISTICS_WORKER_H
#define PNG_ANALYZER_GUI_STATISTICS_WORKER_H

// WP-602G: the dedicated low-priority statistics worker. One QThread runs
// the Qt-free whole-document collector with a cooperative cancellation token
// and publishes immutable shared results through two signals; the session
// generation-gates both before any subscriber sees them. The worker owns the
// request's source/chunk/StageSet copies so a newer document never
// invalidates an in-flight job.

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/statistics_collector.h>

#include <QObject>
#include <QThread>

#include <cstdint>
#include <memory>

Q_DECLARE_METATYPE(
    std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>)

class StatisticsWorker final : public QThread {
  Q_OBJECT
 public:
  StatisticsWorker(pnga::analysis_engine::StatisticsCollectionRequest request,
                   QObject* parent = nullptr);

  // Cooperative cancellation: the collector stops at the next section
  // boundary or check interval and keeps the verified prefix.
  void cancel() noexcept;

 signals:
  // Throttled immutable progress copies (at most ten per second from the
  // collector) and the final result, both carrying the request generation.
  void progress(std::uint64_t generation,
                std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
                    result);
  void finishedResult(
      std::uint64_t generation,
      std::shared_ptr<const pnga::analysis_engine::StatisticsCollectionResult>
          result);

 protected:
  void run() override;

 private:
  pnga::analysis_engine::StatisticsCollectionRequest request_;
  std::shared_ptr<pnga::analysis_engine::CancellationToken> cancellation_;
};

#endif  // PNG_ANALYZER_GUI_STATISTICS_WORKER_H
