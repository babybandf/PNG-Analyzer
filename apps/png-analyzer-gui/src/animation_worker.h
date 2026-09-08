#ifndef PNG_ANALYZER_GUI_ANIMATION_WORKER_H
#define PNG_ANALYZER_GUI_ANIMATION_WORKER_H

#include <pnga/analysis-engine/animation_replay.h>
#include <pnga/analysis-engine/job_scheduler.h>

#include <QThread>

#include <memory>

class AnimationWorker final : public QThread {
  Q_OBJECT
 public:
  AnimationWorker(pnga::analysis_engine::ReplayRequest request,
                  std::uint64_t serial, QObject* parent = nullptr);

  void cancel() noexcept { token_.request_cancel(); }

 signals:
  void finishedResult(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);

 protected:
  void run() override;

 private:
  pnga::analysis_engine::ReplayRequest request_;
  std::uint64_t serial_ = 0;
  pnga::analysis_engine::CancellationToken token_;
};

#endif  // PNG_ANALYZER_GUI_ANIMATION_WORKER_H
