#include "animation_controller.h"

#include "document_session.h"
#include "main_window_ui.h"
#include "selection_navigation_controller.h"

#include <pnga/ui/qt/stage_inspector.h>
#include <algorithm>
#include <pnga/ui/qt/animation_inspector.h>
#include <pnga/ui/qt/animation_timeline.h>
#include <pnga/ui/qt/delivered_image_view.h>

#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/virtual_frame_stream.h>
#include <pnga/analysis-engine/frame_analysis.h>

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
  playback_timer_.stop();
  for (auto* worker : findChildren<AnimationWorker*>()) { worker->cancel(); worker->wait(); }
  for (auto* worker : findChildren<ThumbnailWorker*>()) { worker->cancel(); worker->wait(); }
}

void AnimationController::setDocument(
    const pnga::analysis_engine::FrameRequest& document_context) {
  cancelWorker();
  playback_timer_.stop();
  if (thumbnail_worker_) thumbnail_worker_->cancel();
  thumbnail_queue_.clear();
  thumbnail_pending_.clear();
  generation_ = document_context.generation;
  replay_ = std::make_shared<pnga::analysis_engine::AnimationReplay>(64ull * 1024 * 1024);
  stage_ = pnga::trace_model::Stage::kFrameOutput;
  showing_static_ = false;
  document_context_ = document_context;
  document_context_.generation = generation_;
  ++request_serial_;
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
  if (capability_ == Capability::kValid || capability_ == Capability::kPartial) {
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
  showing_static_ = false;
  selected_ordinal_ = ordinal;
  startFrameWorker(ordinal);
  notifyPlayback();
}

void AnimationController::selectStaticFallback() {
  showing_static_ = true;
  pause();
  cancelWorker();
  ++request_serial_;
  emit staticFallbackSelected();
}

void AnimationController::play() {
  if (playback_) {
    playback_->play(nowNs());
    advancePlayback();
    playback_timer_.start();
    notifyPlayback();
  }
}

void AnimationController::pause() {
  if (playback_) {
    playback_->pause();
    playback_timer_.stop();
    notifyPlayback();
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
  if (thumbnail_worker_) thumbnail_worker_->cancel();
  thumbnail_queue_.clear();
  thumbnail_pending_.clear();
  document_context_ = {};
  playback_.reset();
  capability_ = Capability::kDetecting;
  ++request_serial_;
  emit capabilityChanged(static_cast<int>(capability_));
}

void AnimationController::publishWorkerResultForTesting(
    std::shared_ptr<const pnga::analysis_engine::ReplayResult> result) {
  onWorkerResult(std::move(result));
}

void AnimationController::advancePlaybackForTesting(std::uint64_t now_ns) {
  test_time_ = now_ns;
  advancePlaybackAt(now_ns);
}

void AnimationController::cancelWorker() {
  frame_pending_ = false;
  if (worker_) worker_->cancel();
}
void AnimationController::startFrameWorker(std::uint32_t ordinal) {
  selected_ordinal_ = ordinal;
  ++request_serial_;
  frame_pending_ = true;
  if (worker_) { worker_->cancel(); return; }
  launchPendingFrame();
}
void AnimationController::launchPendingFrame() {
  if (!frame_pending_ || !document_context_.source) return;
  frame_pending_ = false;
  auto request = document_context_;
  request.ordinal = selected_ordinal_;
  request.request_serial = request_serial_;
  auto* worker = new AnimationWorker({request, stage_}, request_serial_, this, replay_);
  worker_ = worker;
  connect(worker, &AnimationWorker::finishedResult, this, &AnimationController::onWorkerResult);
  connect(worker, &QThread::finished, this, [this, worker] {
    if (worker_ == worker) worker_ = nullptr;
    worker->deleteLater();
    launchPendingFrame();
  });
  worker->start();
}
void AnimationController::selectStage(pnga::trace_model::Stage stage) {
  if (stage_ == stage && !showing_static_) return;
  showing_static_ = false;
  pause();
  stage_ = stage;
  if (document_context_.index && !document_context_.index->frames.empty()) startFrameWorker(selected_ordinal_);
}
void AnimationController::notifyPlayback() {
  using pnga::analysis_engine::PlaybackState;
  emit playbackChanged(static_cast<int>(playback_ ? playback_->state() : PlaybackState::kPartial), selected_ordinal_);
}
void AnimationController::requestThumbnail(std::uint32_t ordinal) {
  if (!document_context_.index || ordinal >= document_context_.index->frames.size() ||
      thumbnail_pending_.contains(ordinal) || thumbnail_queue_.size() >= 32) return;
  thumbnail_pending_.insert(ordinal);
  thumbnail_queue_.push_back(ordinal);
  launchThumbnail();
}
void AnimationController::launchThumbnail() {
  if (thumbnail_worker_ || thumbnail_queue_.empty() || !document_context_.source) return;
  auto request = document_context_;
  request.ordinal = thumbnail_queue_.front();
  thumbnail_queue_.pop_front();
  auto* worker = new ThumbnailWorker(request, this);
  thumbnail_worker_ = worker;
  connect(worker, &ThumbnailWorker::ready, this,
      [this](std::uint64_t generation, std::uint32_t ordinal, const QImage& image) {
        if (generation == generation_) emit thumbnailReady(ordinal, image);
      });
  connect(worker, &QThread::finished, this, [this, worker, request] {
    thumbnail_worker_ = nullptr;
    if (request.generation == generation_) thumbnail_pending_.remove(request.ordinal);
    worker->deleteLater();
    launchThumbnail();
  });
  worker->start();
}

void AnimationController::advancePlayback() { advancePlaybackAt(nowNs()); notifyPlayback(); }

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
  return test_time_.value_or(static_cast<std::uint64_t>(clock_.nsecsElapsed()));
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
  if (result->stop != pnga::analysis_engine::ReplayResult::Stop::kReady || !result->image) {
    pause();
    if (result->stop != pnga::analysis_engine::ReplayResult::Stop::kCancelled)
      emit animationError(QString::fromStdString(result->reason));
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
  notifyPlayback();
}

void bindAnimationUi(AnimationController& controller, DocumentSession& session,
                     MainWindowWidgets& widgets,
                     SelectionNavigationController& selection,
                     pnga::gui::FrameInspectionSession& frame_inspection,
                     TraceController& trace) {
  QObject::connect(widgets.x_spin, &QSpinBox::valueChanged, &controller, &AnimationController::pause);
  QObject::connect(widgets.y_spin, &QSpinBox::valueChanged, &controller, &AnimationController::pause);
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
        // Palette/tRNS frames need the document delivery context; without
        // it deliver_rgba8 rejects type-3 frames with "palette is missing".
        request.delivery = pnga::analysis_engine::delivery_context_from(
            *request.index, request.canvas_header);
        controller.setDocument(request);
        if (controller.capability() == AnimationController::Capability::kValid ||
            controller.capability() == AnimationController::Capability::kPartial) {
          mountAnimationUi(widgets, *request.index, &controller);
          for (auto* view : widgets.animation_views) {
            // WP-APNG-INSPECT T09 (C6/D2): every user event of the four
            // animation views routes to the selection controller exactly
            // once; UniqueConnection keeps repeated mounts from doubling
            // publications.
            QObject::connect(view, &pnga::ui::qt::DeliveredImageView::pixelSelected,
                             &selection, &SelectionNavigationController::onAnimationPixelSelected,
                             Qt::UniqueConnection);
            QObject::connect(view, &pnga::ui::qt::DeliveredImageView::pixelHovered,
                             &selection, &SelectionNavigationController::onAnimationFrameHovered,
                             Qt::UniqueConnection);
            QObject::connect(view, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
                             &selection, &SelectionNavigationController::onPixelHoverLeft,
                             Qt::UniqueConnection);
            QObject::connect(view, &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested,
                             &selection, &SelectionNavigationController::nudgeLockedCoordinate,
                             Qt::UniqueConnection);
            QObject::connect(view, &pnga::ui::qt::DeliveredImageView::selectionCancelled,
                             &selection, &SelectionNavigationController::clearLockedCoordinate,
                             Qt::UniqueConnection);
          }
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
  // WP-APNG-INSPECT live wiring (D5): frame analyses flow through the
  // inspection session into the Reconstruction panel and the frame-scoped
  // Hex sources; the Compression panel opens the frame target stream.
  std::uint64_t frame_selection_serial = 0;
  std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> active_target;
  QObject::connect(
      &frame_inspection, &pnga::gui::FrameInspectionSession::frameAnalysisReady,
      &controller,
      [&](std::shared_ptr<const pnga::analysis_engine::FrameStageSet> frame,
          const pnga::trace_model::InspectionTicket& ticket) {
        widgets.inspector->setFrameContext(frame);
        selection.setFrameStageContext(frame);
        selection.setImageIdentity(ticket.key.identity);
        if (active_target != nullptr) {
          trace.setFrameContext(active_target, frame);
        }
      });
  QObject::connect(
      &frame_inspection, &pnga::gui::FrameInspectionSession::frameAnalysisFailed,
      &controller,
      [&widgets](const pnga::trace_model::InspectionTicket&,
                 const QString& error) {
        widgets.inspector->setRowQueryStatus(
            QObject::tr("frame analysis unavailable: %1").arg(error));
      });
  QObject::connect(
      &frame_inspection, &pnga::gui::FrameInspectionSession::targetSelected,
      &controller,
      [&, active_target =
              std::shared_ptr<const pnga::analysis_engine::AnalysisTarget>()](
          const std::shared_ptr<const pnga::analysis_engine::AnalysisTarget>&
              target,
          const pnga::trace_model::InspectionTicket&) mutable {
        active_target = target;
        // The frame stage set arrives with the analysis result.
        trace.setFrameContext(target, nullptr);
      });

  QObject::connect(
      &controller, &AnimationController::framePublished, &controller,
      [&](std::shared_ptr<const pnga::analysis_engine::ReplayResult> result) {
        if (!result || result->generation != session.generation()) return;
        presentAnimationFrame(widgets, *result);
        const auto* frame = std::get_if<pnga::trace_model::AnimationFrame>(
            &result->identity);
        if (frame && session.source() && session.animationIndex()) {
          selection.setImageIdentity(*frame);
          using pnga::trace_model::Stage;
          const std::array stages{Stage::kFrameOutput, Stage::kPreBlend, Stage::kPostBlend, Stage::kPostDispose};
          const auto stage_it = std::find(stages.begin(), stages.end(), result->stage);
          if (stage_it != stages.end()) {
            const auto& control = session.animationIndex()->frames[frame->index].control;
            selection.setAnimationView(widgets.animation_views[stage_it - stages.begin()], result->stage,
                result->stage == Stage::kFrameOutput ? control.x : 0,
                result->stage == Stage::kFrameOutput ? control.y : 0);
            // Explain why a canvas stage can legitimately be fully
            // transparent; the view only shows the hint when the scan finds
            // no opaque pixel.
            QString empty_reason;
            if (result->stage != Stage::kFrameOutput) {
              if (frame->index == 0) {
                empty_reason = QObject::tr(
                    "Empty canvas · playback starts from transparent black");
              } else {
                const auto prev =
                    session.animationIndex()->frames[frame->index - 1].control.dispose;
                if (prev == 1) {
                  empty_reason = QObject::tr(
                      "Empty canvas · previous frame BACKGROUND dispose "
                      "cleared the canvas");
                } else if (prev == 2) {
                  empty_reason = QObject::tr(
                      "Empty canvas · previous frame PREVIOUS dispose "
                      "restored an earlier state");
                }
              }
            }
            widgets.animation_views[stage_it - stages.begin()]
                ->setEmptyCanvasHint(empty_reason);
          }
          selection.setAnimationFrameStream(
              pnga::png_format::make_frame_stream(
                  session.source(), session.animationIndex(), frame->index));
          if (widgets.animation_inspector &&
              frame->index < session.animationIndex()->frames.size()) {
            widgets.animation_inspector->setFrameControl(
                session.animationIndex()->frames[frame->index].control);
          }
          // Route the frame analysis through the inspection session (all
          // decode work happens in its background worker; the UI thread
          // never reads or decodes).
          pnga::analysis_engine::FrameRequest frame_request;
          frame_request.generation = session.generation();
          frame_request.request_serial = result->request_serial;
          frame_request.ordinal = frame->index;
          frame_request.source = session.source();
          frame_request.index = session.animationIndex();
          if (session.stageSet() != nullptr) {
            frame_request.canvas_header = session.stageSet()->header;
            frame_request.delivery = pnga::analysis_engine::delivery_context_from(
                *session.animationIndex(), session.stageSet()->header);
          }
          const pnga::trace_model::InspectionTicket ticket{
              {session.generation(), *frame},
              result->stage, 1, ++frame_selection_serial};
          frame_inspection.openFrame(frame_request, ticket);
        }
      });
  QObject::connect(&controller, &AnimationController::staticFallbackSelected,
                   &controller, [&selection, &session, &widgets, &frame_inspection,
                                 &trace] {
                     const auto& decoded = session.decodeResult();
                     if (decoded.success) widgets.inspector->setDeliveredPixels(
                         decoded.image.width, decoded.image.height, decoded.image.rgba);
                     // Restore the complete static context (C6: the default
                     // image returns to the document analysis).
                     if (session.stageSet() != nullptr) {
                       widgets.inspector->setStageSet(session.stageSet());
                     }
                     selection.setFrameStageContext(nullptr);
                     selection.setImageIdentity(
                         pnga::trace_model::StaticImage{});
                     selection.setAnimationFrameStream(nullptr);
                     trace.setFrameContext(nullptr, nullptr);
                     frame_inspection.clearEvidenceFocus();
                   });
}
