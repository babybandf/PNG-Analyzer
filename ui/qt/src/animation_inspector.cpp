#include "pnga/ui/qt/animation_inspector.h"

#include <QLabel>
#include <QVBoxLayout>

namespace pnga::ui::qt {

AnimationInspector::AnimationInspector(QWidget* parent) : QWidget(parent) {
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(summary_);
}

void AnimationInspector::setFrameControl(
    const pnga::png_format::FrameControl& control) {
  control_ = control;
  const double effective_ms = qMax(10.0, 1000.0 * control.delay_num /
      (control.delay_den == 0 ? 100 : control.delay_den) / speed_);
  summary_text_ = QStringLiteral("Frame rectangle: %1×%2 at (%3,%4)\nsequence %5\nRaw delay: %6/%7 s\nEffective delay: %8 ms at %9×\nBlend: %10\nDispose: %11\nLoops: %12")
      .arg(control.width).arg(control.height).arg(control.x).arg(control.y)
      .arg(control.sequence).arg(control.delay_num).arg(control.delay_den)
      .arg(effective_ms, 0, 'f', 1).arg(speed_)
      .arg(control.blend == 0 ? "SOURCE" : "OVER")
      .arg(control.dispose == 0 ? "NONE" : control.dispose == 1 ? "BACKGROUND" : "PREVIOUS")
      .arg(loops_ == 0 ? QStringLiteral("Infinite") : QString::number(loops_));
  summary_->setText(summary_text_);
}

void AnimationInspector::setPlaybackContext(double speed, std::uint32_t loops) {
  speed_ = speed; loops_ = loops; setFrameControl(control_);
}

QString AnimationInspector::summaryText() const { return summary_text_; }

}  // namespace pnga::ui::qt
