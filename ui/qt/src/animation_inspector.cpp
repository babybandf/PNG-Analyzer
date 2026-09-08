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
  summary_text_ = QStringLiteral("sequence %1 · %2×%3 at (%4,%5) · delay %6/%7 · blend %8 · dispose %9")
                      .arg(control.sequence)
                      .arg(control.width)
                      .arg(control.height)
                      .arg(control.x)
                      .arg(control.y)
                      .arg(control.delay_num)
                      .arg(control.delay_den)
                      .arg(control.blend)
                      .arg(control.dispose);
  summary_->setText(summary_text_);
}

QString AnimationInspector::summaryText() const { return summary_text_; }

}  // namespace pnga::ui::qt
