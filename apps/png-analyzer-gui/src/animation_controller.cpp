#include "animation_controller.h"

#include "document_session.h"
#include "main_window_ui.h"
#include "selection_navigation_controller.h"

#include <pnga/ui/qt/animation_inspector.h>

#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/virtual_frame_stream.h>

#include <variant>
#include <utility>

AnimationController::AnimationController(QObject* parent)
    : QObject(parent), playback_timer_(this) {
  qRegisterMetaType<
      std::shared_ptr<const pnga::analysis_engine::ReplayResult>>();
  clock_.start();
  playback_timer_.setInterval(10);
  connect(&playback_timer_, &QTimer::timeout, this,
          &AnimationController::advancePlayback);
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
  if (capability_ == Capability::kValid) {
    // A complete animation always opens paused on its first verified frame.
    selectFrame(0);
  }
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
  if (playback_) {
    playback_->pause();
    playback_->seek(ordinal, nowNs());
    playback_timer_.stop();
  }
  selected_ordinal_ = ordinal;
  startFrameWorker(ordinal);
}

void AnimationController::selectStaticFallback() {
  cancelWorker();
  if (playback_) {
    playback_->pause();
  }
  emit staticFallbackSelected();
}

void AnimationController::play() {
  if (playback_) {
    playback_->play(nowNs());
    advancePlayback();
    playback_timer_.start();
  }
}

void AnimationController::pause() {
  if (playback_) {
    playback_->pause();
    playback_timer_.stop();
  }
}

void AnimationController::setSpeed(
    pnga::analysis_engine::PlaybackSpeed speed) {
  if (playback_) {
    playback_->set_speed(speed, nowNs());
  }
}

void AnimationController::close() {
  cancelWorker();
  playback_timer_.stop();
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

void AnimationController::advancePlaybackForTesting(std::uint64_t now_ns) {
  advancePlaybackAt(now_ns);
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
  selected_ordinal_ = ordinal;
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

void AnimationController::advancePlayback() { advancePlaybackAt(nowNs()); }

void AnimationController::advancePlaybackAt(std::uint64_t now_ns) {
  if (!playback_ || !document_context_.index) return;
  const auto ordinal = playback_->tick(now_ns);
  if (ordinal.has_value()) {
    startFrameWorker(*ordinal);
  }
  if (playback_->state() == pnga::analysis_engine::PlaybackState::kEnded ||
      playback_->state() == pnga::analysis_engine::PlaybackState::kError ||
      playback_->state() == pnga::analysis_engine::PlaybackState::kPaused) {
    playback_timer_.stop();
  }
}

std::uint64_t AnimationController::nowNs() const noexcept {
  return static_cast<std::uint64_t>(clock_.nsecsElapsed());
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
  if (playback_ &&
      playback_->state() ==
          pnga::analysis_engine::PlaybackState::kWaitingForFrame &&
      playback_->current_ordinal() == selected_ordinal_) {
    playback_->frame_ready(selected_ordinal_, playback_->request_serial(),
                           nowNs());
  }
  emit framePublished(std::move(result));
}

void bindAnimationUi(AnimationController& controller, DocumentSession& session,
                     MainWindowWidgets& widgets,
                     SelectionNavigationController& selection) {
  QObject::connect(
      &session, &DocumentSession::animationPublished, &controller,
      [&controller, &session, &widgets, &selection](
          std::uint64_t generation,
          std::shared_ptr<const pnga::png_format::AnimationIndex> index) {
        if (generation != session.generation() || !index ||
            !session.stageSet()) {
          return;
        }
        pnga::analysis_engine::FrameRequest request;
        request.generation = generation;
        request.source = session.source();
        request.index = std::move(index);
        request.canvas_header = session.stageSet()->header;
        controller.setDocument(request);
        if (controller.capability() == AnimationController::Capability::kValid ||
            controller.capability() == AnimationController::Capability::kPartial) {
          mountAnimationUi(widgets, *request.index, &controller);
          if (!request.index->frames.empty()) {
            // The default frame is selected immediately by setDocument; make
            // the Hex source presentation follow it before its worker result.
            auto stream = pnga::png_format::make_frame_stream(
                session.source(), request.index, 0);
            if (stream != nullptr) {
              selection.setImageIdentity(
                  pnga::trace_model::AnimationFrame{0});
              selection.setAnimationFrameStream(std::move(stream));
            }
          }
        }
      });
  QObject::connect(
      &controller, &AnimationController::framePublished, &controller,
      [&session, &widgets, &selection](
          std::shared_ptr<const pnga::analysis_engine::ReplayResult> result) {
        if (!result || result->generation != session.generation()) return;
        presentAnimationFrame(widgets, *result);
        const auto* frame = std::get_if<pnga::trace_model::AnimationFrame>(
            &result->identity);
        if (frame && session.source() && session.animationIndex()) {
          selection.setImageIdentity(*frame);
          selection.setAnimationFrameStream(
              pnga::png_format::make_frame_stream(
                  session.source(), session.animationIndex(), frame->index));
          if (widgets.animation_inspector &&
              frame->index < session.animationIndex()->frames.size()) {
            widgets.animation_inspector->setFrameControl(
                session.animationIndex()->frames[frame->index].control);
          }
        }
      });
  QObject::connect(&controller, &AnimationController::staticFallbackSelected,
                   &controller, [&selection] {
                     selection.setImageIdentity(
                         pnga::trace_model::StaticImage{});
                     selection.setAnimationFrameStream(nullptr);
                   });
}
