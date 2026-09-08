#include "animation_controller.h"

#include <pnga/png-format/animation_index.h>

#include <utility>

AnimationController::AnimationController(QObject* parent) : QObject(parent) {
  qRegisterMetaType<
      std::shared_ptr<const pnga::analysis_engine::ReplayResult>>();
}

AnimationController::~AnimationController() {
  cancelWorker();
  if (worker_ != nullptr && worker_->isRunning()) {
    worker_->wait();
  }
}

void AnimationController::setDocument(
    const pnga::analysis_engine::FrameRequest& document_context) {
  cancelWorker();
  ++generation_;
  document_context_ = document_context;
  document_context_.generation = generation_;
  request_serial_ = 0;
  selected_ordinal_ = 0;
  playback_.reset();

  Capability next = Capability::kError;
  if (!document_context_.index) {
    next = Capability::kError;
  } else if (document_context_.index->status ==
             pnga::png_format::AnimationStatus::kStatic) {
    next = Capability::kStatic;
  } else if (document_context_.index->status ==
                 pnga::png_format::AnimationStatus::kComplete &&
             document_context_.index->control.has_value()) {
    next = Capability::kValid;
    const auto timeline = pnga::analysis_engine::make_timeline(
        *document_context_.index,
        pnga::analysis_engine::PlaybackSpeed::kNormal);
    if (timeline.has_value()) {
      playback_ = std::make_unique<pnga::analysis_engine::AnimationPlayback>(
          *timeline, generation_);
    }
  } else if (document_context_.index->status ==
                 pnga::png_format::AnimationStatus::kPartial &&
             !document_context_.index->frames.empty()) {
    next = Capability::kPartial;
  }
  capability_ = next;
  emit capabilityChanged(static_cast<int>(capability_));
}

void AnimationController::selectFrame(std::uint32_t ordinal) {
  if ((capability_ != Capability::kValid && capability_ != Capability::kPartial) ||
      !document_context_.index ||
      ordinal >= document_context_.index->frames.size()) {
    return;
  }
  if (capability_ == Capability::kPartial) {
    pause();
  }
  selected_ordinal_ = ordinal;
  startFrameWorker(ordinal);
}

void AnimationController::selectStaticFallback() {
  cancelWorker();
  if (playback_) {
    playback_->pause();
  }
}

void AnimationController::play() {
  if (playback_) {
    playback_->play(0);
  }
}

void AnimationController::pause() {
  if (playback_) {
    playback_->pause();
  }
}

void AnimationController::setSpeed(
    pnga::analysis_engine::PlaybackSpeed speed) {
  if (playback_) {
    playback_->set_speed(speed, 0);
  }
}

void AnimationController::close() {
  cancelWorker();
  ++generation_;
  document_context_ = {};
  playback_.reset();
  capability_ = Capability::kDetecting;
  request_serial_ = 0;
  emit capabilityChanged(static_cast<int>(capability_));
}

void AnimationController::publishWorkerResultForTesting(
    std::shared_ptr<const pnga::analysis_engine::ReplayResult> result) {
  onWorkerResult(std::move(result));
}

void AnimationController::cancelWorker() {
  if (worker_ == nullptr) {
    return;
  }
  worker_->cancel();
  if (worker_->isRunning()) {
    worker_->wait();
  }
  worker_->deleteLater();
  worker_ = nullptr;
}

void AnimationController::startFrameWorker(std::uint32_t ordinal) {
  cancelWorker();
  ++request_serial_;
  document_context_.request_serial = request_serial_;
  document_context_.ordinal = ordinal;
  pnga::analysis_engine::ReplayRequest replay_request{
      document_context_, pnga::trace_model::Stage::kFrameOutput};
  auto* worker = new AnimationWorker(std::move(replay_request),
                                     request_serial_, this);
  worker_ = worker;
  connect(worker, &AnimationWorker::finishedResult, this,
          &AnimationController::onWorkerResult);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  connect(worker, &QThread::finished, this, [this, worker] {
    if (worker_ == worker) {
      worker_ = nullptr;
    }
  });
  worker->start();
}

void AnimationController::onWorkerResult(
    std::shared_ptr<const pnga::analysis_engine::ReplayResult> result) {
  if (!result || result->generation != generation_ ||
      result->request_serial != request_serial_ ||
      result->identity != pnga::trace_model::ImageIdentity{
                                  pnga::trace_model::AnimationFrame{
                                      selected_ordinal_}}) {
    return;
  }
  emit framePublished(std::move(result));
}
