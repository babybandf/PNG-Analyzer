#include "animation_worker.h"

AnimationWorker::AnimationWorker(pnga::analysis_engine::ReplayRequest request,
                                 std::uint64_t serial, QObject* parent)
    : QThread(parent), request_(std::move(request)), serial_(serial) {
  request_.frame.request_serial = serial_;
}

void AnimationWorker::run() {
  pnga::analysis_engine::AnimationReplay replay(64ull * 1024 * 1024);
  auto result = std::make_shared<pnga::analysis_engine::ReplayResult>(
      replay.materialize(request_, &token_));
  emit finishedResult(std::move(result));
}
