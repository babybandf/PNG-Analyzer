// WP-204 DeliveredImageView: zoom/pan rendering of the delivered RGBA image.

#include "pnga/ui/qt/delivered_image_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <array>
#include <cstdint>
#include <optional>

namespace pnga::ui::qt {

DeliveredImageView::DeliveredImageView(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
}

void DeliveredImageView::setImage(const QImage& image) {
  image_ = image;
  refit();
  update();
}

std::optional<std::array<std::uint8_t, 4>> DeliveredImageView::rgbaAt(
    int x, int y) const {
  if (image_.isNull() || x < 0 || y < 0 || x >= image_.width() ||
      y >= image_.height()) {
    return std::nullopt;
  }
  const QRgb px = image_.pixel(x, y);
  return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(qRed(px)),
                                     static_cast<std::uint8_t>(qGreen(px)),
                                     static_cast<std::uint8_t>(qBlue(px)),
                                     static_cast<std::uint8_t>(qAlpha(px))};
}

QPoint DeliveredImageView::imagePointAt(const QPoint& widgetPoint) const {
  const QPointF p = transform_.widgetToImage(QPointF(widgetPoint));
  return QPoint(static_cast<int>(std::round(p.x())),
                static_cast<int>(std::round(p.y())));
}

void DeliveredImageView::refit() {
  transform_.fit(size(), image_.size());
}

void DeliveredImageView::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), palette().color(QPalette::Window));
  if (image_.isNull()) {
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
    return;
  }
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QPointF origin = transform_.imageToWidget(QPointF(0, 0));
  const QSizeF scaled = image_.size() * transform_.zoom();
  p.drawImage(QRectF(origin, scaled), image_);
}

void DeliveredImageView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  refit();
}

void DeliveredImageView::wheelEvent(QWheelEvent* event) {
  const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  transform_.zoomBy(factor, QPointF(event->position()));
  update();
  event->accept();
}

void DeliveredImageView::mousePressEvent(QMouseEvent* event) {
  lastMouse_ = event->pos();
  panning_ = true;
  event->accept();
}

void DeliveredImageView::mouseMoveEvent(QMouseEvent* event) {
  if (panning_) {
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    transform_.panBy(QPointF(delta));
    update();
  }
  event->accept();
}

void DeliveredImageView::mouseReleaseEvent(QMouseEvent* event) {
  if (panning_) {
    panning_ = false;
    // A click without dragging publishes a pixel selection.
    if (event->pos() == lastMouse_ && !image_.isNull()) {
      const QPoint ip = imagePointAt(event->pos());
      if (ip.x() >= 0 && ip.y() >= 0 && ip.x() < image_.width() &&
          ip.y() < image_.height()) {
        emit pixelSelected(ip.x(), ip.y());
      }
    }
  }
  event->accept();
}

}  // namespace pnga::ui::qt
