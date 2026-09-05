// WP-602G: StatisticsWorker implementation. The run() method executes the
// Qt-free collect_document_statistics pipeline off the UI thread with a
// cooperative cancellation token, forwarding every throttled immutable
// progress copy and the final result through queued signals. Metatypes are
// registered before any emission so the queued cross-thread delivery and
// signal spies work.

#include "statistics_worker.h"

#include <utility>

StatisticsWorker::StatisticsWorker(
    pnga::analysis_engine::StatisticsCollectionRequest request,
    QObject* parent)
    : QThread(parent),
      request_(std::move(request)),
      cancellation_(
          std::make_shared<pnga::analysis_engine::CancellationToken>()) {
  qRegisterMetaType<std::shared_ptr<
      const pnga::analysis_engine::StatisticsCollectionResult>>(
      "std::shared_ptr<const "
      "pnga::analysis_engine::StatisticsCollectionResult>");
}

void StatisticsWorker::cancel() noexcept {
  cancellation_->request_cancel();
}

void StatisticsWorker::run() {
  pnga::analysis_engine::StatisticsCollectionResult result =
      pnga::analysis_engine::collect_document_statistics(
          request_, cancellation_.get(),
          [this](const pnga::analysis_engine::StatisticsCollectionResult&
                     partial,
                 const pnga::analysis_engine::StatisticsProgress&) {
            emit progress(partial.generation,
                          std::make_shared<
                              const pnga::analysis_engine::
                                  StatisticsCollectionResult>(partial));
          });
  emit finishedResult(
      result.generation,
      std::make_shared<const pnga::analysis_engine::StatisticsCollectionResult>(
          std::move(result)));
}
