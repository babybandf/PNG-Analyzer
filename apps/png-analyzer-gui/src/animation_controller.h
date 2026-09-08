#ifndef PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H
#define PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H

#include "animation_worker.h"

#include <pnga/analysis-engine/animation_playback.h>

#include <QObject>
#include <QElapsedTimer>
#include <QMetaType>
#include <QTimer>
#include <QImage>
#include <deque>
#include <QSet>

#include <cstdint>
#include <memory>

struct MainWindowWidgets;
class DocumentSession;
class SelectionNavigationController;

Q_DECLARE_METATYPE(
    std::shared_ptr<const pnga::analysis_engine::ReplayResult>)

class AnimationController final : public QObject {
  Q_OBJECT
 public:
  enum class Capability { kDetecting, kStatic, kValid, kPartial, kError };

  explicit AnimationController(QObject* parent = nullptr);
  ~AnimationController() override;

  void setDocument(const pnga::analysis_engine::FrameRequest& document_context);
  void selectFrame(std::uint32_t ordinal);
  void selectStaticFallback();
  void play();
  void pause();
  void setSpeed(pnga::analysis_engine::PlaybackSpeed speed);
  void close();
  void selectStage(pnga::trace_model::Stage stage);
  void requestThumbnail(std::uint32_t ordinal);

  Capability capability() const noexcept { return capability_; }
  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t requestSerial() const noexcept { return request_serial_; }

  // Test seam for delivering a deliberately reordered worker completion.
  void publishWorkerResultForTesting(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);
  void advancePlaybackForTesting(std::uint64_t now_ns);

 signals:
  void capabilityChanged(int capability);
  void framePublished(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);
  void staticFallbackSelected();
  void playbackChanged(int state, std::uint32_t ordinal);
  void animationError(const QString& reason);
  void thumbnailReady(std::uint32_t ordinal, const QImage& image);

 private slots:
  void onWorkerResult(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);

 private:
  void cancelWorker();
  void startFrameWorker(std::uint32_t ordinal);
  void launchPendingFrame();
  void launchThumbnail();
  void notifyPlayback();
  void advancePlayback();
  void advancePlaybackAt(std::uint64_t now_ns);
  std::uint64_t nowNs() const noexcept;

  pnga::analysis_engine::FrameRequest document_context_;
  AnimationWorker* worker_ = nullptr;
  std::unique_ptr<pnga::analysis_engine::AnimationPlayback> playback_;
  std::shared_ptr<pnga::analysis_engine::AnimationReplay> replay_;
  bool frame_pending_ = false;
  bool showing_static_ = false;
  pnga::trace_model::Stage stage_ = pnga::trace_model::Stage::kFrameOutput;
  ThumbnailWorker* thumbnail_worker_ = nullptr;
  std::deque<std::uint32_t> thumbnail_queue_;
  QSet<std::uint32_t> thumbnail_pending_;
  std::optional<std::uint64_t> test_time_;
  QElapsedTimer clock_;
  QTimer playback_timer_;
  Capability capability_ = Capability::kDetecting;
  std::uint64_t generation_ = 0;
  std::uint64_t request_serial_ = 0;
  std::uint32_t selected_ordinal_ = 0;
};

void bindAnimationUi(AnimationController& controller, DocumentSession& session,
                     MainWindowWidgets& widgets,
                     SelectionNavigationController& selection);

#endif  // PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H
