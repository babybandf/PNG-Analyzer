#include "pnga/ui/qt/animation_timeline.h"

#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

namespace pnga::ui::qt {

AnimationTimelineWidget::AnimationTimelineWidget(QWidget* parent)
    : QWidget(parent), model_(new AnimationTimelineModel(this)),
      list_(new QListView(this)) {
  list_->setModel(model_);
  list_->setUniformItemSizes(true);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(list_);
  auto* play = new QPushButton(tr("Play"), this);
  layout->addWidget(play);
  connect(play, &QPushButton::clicked, this,
          &AnimationTimelineWidget::playRequested);
  connect(list_, &QListView::activated, this,
          [this](const QModelIndex& index) {
            emit frameRequested(static_cast<std::uint32_t>(index.row()));
          });
}

void AnimationTimelineWidget::setTimeline(
    const pnga::analysis_engine::AnimationTimeline& timeline) {
  model_->setTimeline(timeline);
}

void AnimationTimelineWidget::requestFrameForTesting(std::uint32_t ordinal) {
  emit frameRequested(ordinal);
}

void AnimationTimelineWidget::requestPlayForTesting() { emit playRequested(); }

void AnimationTimelineWidget::requestSpeedForTesting(int speed) {
  emit speedRequested(speed);
}

}  // namespace pnga::ui::qt
