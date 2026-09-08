#ifndef PNGA_UI_QT_ANIMATION_TIMELINE_H
#define PNGA_UI_QT_ANIMATION_TIMELINE_H

#include "pnga/ui/qt/animation_timeline_model.h"

#include <QWidget>

class QListView;

namespace pnga::ui::qt {

class AnimationTimelineWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit AnimationTimelineWidget(QWidget* parent = nullptr);

  void setTimeline(const pnga::analysis_engine::AnimationTimeline& timeline);
  AnimationTimelineModel* model() const noexcept { return model_; }

  // Small deterministic seams used by Qt tests and keyboard/action wiring.
  void requestFrameForTesting(std::uint32_t ordinal);
  void requestPlayForTesting();
  void requestSpeedForTesting(int speed);

 signals:
  void frameRequested(std::uint32_t ordinal);
  void playRequested();
  void pauseRequested();
  void speedRequested(int speed);

 private:
  AnimationTimelineModel* model_ = nullptr;
  QListView* list_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_ANIMATION_TIMELINE_H
