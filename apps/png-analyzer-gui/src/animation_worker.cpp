#include "animation_worker.h"
#include <QDebug>
#include <climits>

AnimationWorker::AnimationWorker(pnga::analysis_engine::ReplayRequest request,
                                 std::uint64_t serial, QObject* parent,
                                 std::shared_ptr<pnga::analysis_engine::AnimationReplay> replay)
    : QThread(parent), request_(std::move(request)), replay_(std::move(replay)), serial_(serial) {
  request_.frame.request_serial = serial_;
}
void AnimationWorker::run() {
  if (!replay_) replay_ = std::make_shared<pnga::analysis_engine::AnimationReplay>(64ull * 1024 * 1024);
  auto result = std::make_shared<pnga::analysis_engine::ReplayResult>(
      replay_->materialize(request_, &token_));
  emit finishedResult(std::move(result));
}
void ThumbnailWorker::run() {
  const auto result = pnga::analysis_engine::analyze_frame(request_, &token_);
  QImage thumbnail;
  if (!token_.cancelled() && result.frame && result.stop == pnga::analysis_engine::FrameResult::Stop::kReady) {
    const auto& image = result.frame->delivered;
    if (image.width <= INT_MAX / 4 && image.height <= INT_MAX && image.width && image.height &&
        static_cast<std::uint64_t>(image.width) * image.height <= image.pixels.size() / 4) {
      QImage borrowed(image.pixels.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888);
      thumbnail = borrowed.scaled(96, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy();
    }
  }
  if (!token_.cancelled()) emit ready(request_.generation, request_.ordinal, thumbnail);
}
