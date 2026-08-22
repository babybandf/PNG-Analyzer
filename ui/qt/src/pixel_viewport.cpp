// WP-5U3B bounded native-sample viewport presentation.

#include "pnga/ui/qt/pixel_viewport.h"

#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace pnga::ui::qt {

PixelViewport::PixelViewport(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("pixelsViewport"));
  auto* layout = new QVBoxLayout(this);
  label_ = new QLabel(this);
  label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(label_);
  refresh();
}

void PixelViewport::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages) {
  provider_.setStageSet(std::move(stages));
  refresh();
}

void PixelViewport::clear() {
  provider_.clear();
  refresh();
}

void PixelViewport::setCenter(std::uint64_t x, std::uint64_t y) {
  center_x_ = x;
  center_y_ = y;
  refresh();
}

void PixelViewport::refresh() {
  const auto stages = provider_.stageSet();
  if (stages == nullptr || !stages->success || stages->native.width == 0 ||
      stages->native.height == 0 || stages->native.channels == 0) {
    label_->setText(QStringLiteral("Pixels\nStage not available"));
    return;
  }

  const std::uint64_t x = std::min(center_x_,
                                   static_cast<std::uint64_t>(stages->native.width - 1));
  const std::uint64_t y = std::min(center_y_,
                                   static_cast<std::uint64_t>(stages->native.height - 1));
  const std::uint64_t left = x == 0 ? 0 : x - 1;
  const std::uint64_t top = y == 0 ? 0 : y - 1;
  const std::uint64_t width =
      std::min<std::uint64_t>(3, stages->native.width - left);
  const std::uint64_t height =
      std::min<std::uint64_t>(3, stages->native.height - top);
  pnga::analysis_engine::ViewportRequest request;
  request.x = left;
  request.y = top;
  request.width = width;
  request.height = height;
  const auto view = provider_.query(request);
  if (view->status != pnga::analysis_engine::ViewportStatus::kReady) {
    label_->setText(QStringLiteral("Pixels\n%1")
                        .arg(QString::fromLatin1(
                            pnga::analysis_engine::viewport_status_text(
                                view->status))));
    return;
  }

  QString text = QStringLiteral("Native samples (DEC)\nwindow x=%1 y=%2\n")
                     .arg(left)
                     .arg(top);
  std::size_t cursor = 0;
  for (std::uint64_t row = 0; row < view->height; ++row) {
    for (std::uint64_t column = 0; column < view->width; ++column) {
      text += QStringLiteral("(%1,%2): [").arg(left + column).arg(top + row);
      for (std::uint8_t channel = 0; channel < view->channels; ++channel) {
        if (channel != 0) {
          text += QStringLiteral(", ");
        }
        text += QString::number(view->samples[cursor++]);
      }
      text += QStringLiteral("]");
      if (column + 1 == view->width) {
        text += QLatin1Char('\n');
      } else {
        text += QStringLiteral("  ");
      }
    }
  }
  label_->setText(text);
}

}  // namespace pnga::ui::qt
