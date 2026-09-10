#include "pnga/ui/qt/animation_timeline.h"

#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QShortcut>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <functional>

namespace {

class TimelineThumbnailView final : public QListView {
 public:
  using QListView::QListView;

  std::function<void(int)> frameRequested;

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    const auto before = currentIndex();
    QListView::keyPressEvent(event);
    const auto key = event->key();
    if ((key == Qt::Key_Left || key == Qt::Key_Right) &&
        currentIndex().isValid() && currentIndex() != before &&
        frameRequested) {
      frameRequested(currentIndex().row());
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    const auto before = currentIndex();
    QListView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && currentIndex().isValid() &&
        currentIndex() != before && frameRequested) {
      frameRequested(currentIndex().row());
    }
  }
};

}  // namespace

namespace pnga::ui::qt {
AnimationTimelineWidget::AnimationTimelineWidget(QWidget* parent)
    : QWidget(parent), model_(new AnimationTimelineModel(this)),
      list_(new TimelineThumbnailView(this)) {
  setMinimumWidth(0);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto* header = new QHBoxLayout;
  auto* collapse = new QPushButton(tr("Timeline ▾"), this);
  collapse->setCheckable(true);
  collapse->setChecked(true);
  header->addWidget(collapse);
  status_ = new QLabel(tr("Paused"), this);
  status_->setObjectName("animationStatus");
  header->addWidget(status_, 1);
  layout->addLayout(header);
  list_->setObjectName("animationThumbnails");
  list_->setAccessibleName(tr("Frame thumbnails"));
  list_->setModel(model_);
  list_->setViewMode(QListView::IconMode);
  list_->setFlow(QListView::LeftToRight);
  list_->setWrapping(false);
  list_->setMovement(QListView::Static);
  list_->setUniformItemSizes(true);
  list_->setIconSize(QSize(96, 60));
  list_->setGridSize(QSize(112, 90));
  list_->setFixedHeight(110);
  layout->addWidget(list_);
  static_cast<TimelineThumbnailView*>(list_)->frameRequested =
      [this](int ordinal) {
        emit frameRequested(static_cast<std::uint32_t>(ordinal));
      };
  connect(collapse, &QPushButton::toggled, list_, &QWidget::setVisible);
  auto* controls = new QHBoxLayout;
  const auto button = [&](const QString& name, const QString& text, auto action) {
    auto* item = new QPushButton(text, this);
    item->setObjectName(name);
    item->setAccessibleName(text);
    controls->addWidget(item);
    connect(item, &QPushButton::clicked, this, action);
    return item;
  };
  button("animationFirst", tr("First"), [this] { emit frameRequested(0); });
  button("animationPrevious", tr("Previous"), [this] {
    emit frameRequested(static_cast<std::uint32_t>(qMax(0, frame_->value() - 1)));
  });
  play_ = button("animationPlay", tr("Play"), [this] {
    if (playing_) emit pauseRequested(); else emit playRequested();
  });
  button("animationNext", tr("Next"), [this] {
    emit frameRequested(static_cast<std::uint32_t>(qMin(frame_->maximum(), frame_->value() + 1)));
  });
  button("animationLast", tr("Last"), [this] {
    emit frameRequested(static_cast<std::uint32_t>(frame_->maximum()));
  });
  controls->addWidget(new QLabel(tr("Frame"), this));
  frame_ = new QSpinBox(this);
  frame_->setObjectName("animationFrame");
  frame_->setAccessibleName(tr("Current frame"));
  frame_->setKeyboardTracking(false);
  controls->addWidget(frame_);
  speed_ = new QComboBox(this);
  speed_->setObjectName("animationSpeed");
  speed_->setAccessibleName(tr("Playback speed"));
  speed_->addItems({"0.25×", "0.5×", "1×", "2×"});
  speed_->setCurrentIndex(2);
  controls->addWidget(speed_);
  controls->addStretch();
  layout->addLayout(controls);
  fallback_ = new QPushButton(tr("Static fallback · not an animation frame"), this);
  fallback_->setObjectName("animationFallback");
  fallback_->hide();
  layout->addWidget(fallback_);
  connect(fallback_, &QPushButton::clicked, this, &AnimationTimelineWidget::staticFallbackRequested);
  connect(frame_, &QSpinBox::valueChanged, this, [this](int ordinal) { emit frameRequested(ordinal); });
  connect(speed_, &QComboBox::currentIndexChanged, this, &AnimationTimelineWidget::speedRequested);
  connect(list_, &QListView::activated, this, [this](const QModelIndex& i) { emit frameRequested(i.row()); });
  auto* jump = new QShortcut(QKeySequence("Ctrl+G"), this);
  connect(jump, &QShortcut::activated, frame_, [this] { frame_->setFocus(); frame_->selectAll(); });
}

void AnimationTimelineWidget::setTimeline(const pnga::analysis_engine::AnimationTimeline& timeline) {
  timeline_ = timeline;
  model_->setTimeline(timeline);
  frame_->setRange(0, qMax(0, model_->rowCount() - 1));
  play_->setEnabled(timeline.complete);
  speed_->setEnabled(timeline.complete);
  setPlayback(static_cast<int>(timeline.complete ? pnga::analysis_engine::PlaybackState::kPaused : pnga::analysis_engine::PlaybackState::kPartial), 0);
}
void AnimationTimelineWidget::setPlayback(int state, std::uint32_t ordinal) {
  using pnga::analysis_engine::PlaybackState;
  const auto mode = static_cast<PlaybackState>(state);
  playing_ = mode == PlaybackState::kPlaying || mode == PlaybackState::kWaitingForFrame;
  play_->setText(playing_ ? tr("Pause") : tr("Play"));
  const QSignalBlocker block(frame_);
  frame_->setValue(static_cast<int>(ordinal));
  const auto item = model_->index(static_cast<int>(ordinal), 0);
  list_->setCurrentIndex(item);
  list_->scrollTo(item);
  const QString name = mode == PlaybackState::kWaitingForFrame ? tr("Loading") :
      mode == PlaybackState::kPlaying ? tr("Playing") : mode == PlaybackState::kEnded ? tr("Ended") :
      mode == PlaybackState::kPartial ? tr("Partial · playback unavailable") : tr("Paused");
  const auto ns = ordinal < timeline_.entries.size() ? timeline_.entries[ordinal].start_ns : 0;
  status_->setText(tr("%1 · Frame %2 / %3 · %4 ms · Loops %5")
      .arg(name).arg(ordinal).arg(qMax(0, model_->rowCount() - 1)).arg(ns / 1000000)
      .arg(timeline_.num_plays == 0 ? QStringLiteral("∞") : QString::number(timeline_.num_plays)));
}
void AnimationTimelineWidget::setError(const QString& reason) {
  playing_ = false; play_->setText(tr("Play")); play_->setEnabled(false);
  status_->setText(tr("Animation stopped: %1").arg(reason));
}
void AnimationTimelineWidget::setStaticFallbackAvailable(bool available) { fallback_->setVisible(available); }
void AnimationTimelineWidget::requestFrameForTesting(std::uint32_t ordinal) { emit frameRequested(ordinal); }
void AnimationTimelineWidget::requestPlayForTesting() { emit playRequested(); }
void AnimationTimelineWidget::requestSpeedForTesting(int speed) { emit speedRequested(speed); }
}  // namespace pnga::ui::qt
