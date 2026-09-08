#ifndef PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H
#define PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H

#include "animation_worker.h"

#include <pnga/analysis-engine/animation_playback.h>

#include <QObject>
#include <QMetaType>

#include <cstdint>
#include <memory>

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

  Capability capability() const noexcept { return capability_; }
  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t requestSerial() const noexcept { return request_serial_; }

  // Test seam for delivering a deliberately reordered worker completion.
  void publishWorkerResultForTesting(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);

 signals:
  void capabilityChanged(int capability);
  void framePublished(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);

 private slots:
  void onWorkerResult(
      std::shared_ptr<const pnga::analysis_engine::ReplayResult> result);

 private:
  void cancelWorker();
  void startFrameWorker(std::uint32_t ordinal);

  pnga::analysis_engine::FrameRequest document_context_;
  AnimationWorker* worker_ = nullptr;
  std::unique_ptr<pnga::analysis_engine::AnimationPlayback> playback_;
  Capability capability_ = Capability::kDetecting;
  std::uint64_t generation_ = 0;
  std::uint64_t request_serial_ = 0;
  std::uint32_t selected_ordinal_ = 0;
};

#endif  // PNG_ANALYZER_GUI_ANIMATION_CONTROLLER_H
