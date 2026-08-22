// WP-204 DeliveredImageView: zoom/pan rendering of the delivered RGBA image.

#include "pnga/ui/qt/delivered_image_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QEvent>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace pnga::ui::qt {

DeliveredImageView::DeliveredImageView(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
}

void DeliveredImageView::setImage(const QImage& image) {
  image_ = image;
  clearHoverPixel();
  clearLockedPixel();
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
  const auto pixel = imagePixelAt(widgetPoint);
  if (pixel.has_value()) {
    return *pixel;
  }
  const QPointF p = transform_.widgetToImage(QPointF(widgetPoint));
  return QPoint(static_cast<int>(std::floor(p.x())),
                static_cast<int>(std::floor(p.y())));
}

std::optional<QPoint> DeliveredImageView::imagePixelAt(
    const QPoint& widgetPoint) const {
  if (image_.isNull()) {
    return std::nullopt;
  }
  const QPointF origin = transform_.imageToWidget(QPointF(0, 0));
  const QSizeF scaled = image_.size() * transform_.zoom();
  const QPointF point(widgetPoint);
  if (point.x() < origin.x() || point.y() < origin.y() ||
      point.x() >= origin.x() + scaled.width() ||
      point.y() >= origin.y() + scaled.height()) {
    return std::nullopt;
  }
  const QPointF image_point = transform_.widgetToImage(point);
  const int x = static_cast<int>(std::floor(image_point.x()));
  const int y = static_cast<int>(std::floor(image_point.y()));
  if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height()) {
    return std::nullopt;
  }
  return QPoint(x, y);
}

void DeliveredImageView::setHoverPixel(const QPoint& pixel) {
  if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= image_.width() ||
      pixel.y() >= image_.height()) {
    clearHoverPixel();
    return;
  }
  hover_pixel_ = pixel;
  update();
}

void DeliveredImageView::clearHoverPixel() {
  if (hover_pixel_.has_value()) {
    hover_pixel_.reset();
    update();
  }
}

void DeliveredImageView::setLockedPixel(const QPoint& pixel) {
  if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= image_.width() ||
      pixel.y() >= image_.height()) {
    clearLockedPixel();
    return;
  }
  locked_pixel_ = pixel;
  update();
}

void DeliveredImageView::clearLockedPixel() {
  if (locked_pixel_.has_value()) {
    locked_pixel_.reset();
    update();
  }
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

  const auto drawMarker = [this, &p, origin](const QPoint& pixel,
                                              const QColor& color,
                                              Qt::PenStyle style) {
    const qreal zoom = transform_.zoom();
    const QRectF rect(origin.x() + pixel.x() * zoom,
                      origin.y() + pixel.y() * zoom, zoom, zoom);
    QPen pen(color, zoom >= 4.0 ? 2.0 : 1.0, style);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    if (zoom < 4.0) {
      const QPointF center = rect.center();
      p.drawLine(center + QPointF(-4, 0), center + QPointF(4, 0));
      p.drawLine(center + QPointF(0, -4), center + QPointF(0, 4));
    }
  };
  if (hover_pixel_.has_value()) {
    drawMarker(*hover_pixel_, QColor(0xFF, 0xD5, 0x4F), Qt::DashLine);
  }
  if (locked_pixel_.has_value()) {
    drawMarker(*locked_pixel_, QColor(0xF4, 0x43, 0x36), Qt::SolidLine);
  }
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
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  lastMouse_ = event->pos();
  pressPosition_ = event->pos();
  panning_ = true;
  dragged_ = false;
  event->accept();
}

void DeliveredImageView::mouseMoveEvent(QMouseEvent* event) {
  const auto pixel = imagePixelAt(event->pos());
  if (pixel.has_value()) {
    if (!hover_pixel_.has_value() || *hover_pixel_ != *pixel) {
      setHoverPixel(*pixel);
      emit pixelHovered(pixel->x(), pixel->y());
    }
  } else if (hover_pixel_.has_value()) {
    clearHoverPixel();
    emit pixelHoverLeft();
  }
  if (panning_) {
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    if ((event->pos() - pressPosition_).manhattanLength() >=
        QApplication::startDragDistance()) {
      dragged_ = true;
    }
    if (dragged_) {
      transform_.panBy(QPointF(delta));
      update();
    }
  }
  event->accept();
}

void DeliveredImageView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && panning_) {
    panning_ = false;
    // A click without dragging publishes a pixel selection.
    if (!dragged_ && !image_.isNull()) {
      const auto pixel = imagePixelAt(event->pos());
      if (pixel.has_value()) {
        emit pixelSelected(pixel->x(), pixel->y());
      }
    }
  }
  event->accept();
}

void DeliveredImageView::leaveEvent(QEvent* event) {
  clearHoverPixel();
  emit pixelHoverLeft();
  QWidget::leaveEvent(event);
}

void DeliveredImageView::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Left:
      emit pixelNudgeRequested(-1, 0);
      event->accept();
      return;
    case Qt::Key_Right:
      emit pixelNudgeRequested(1, 0);
      event->accept();
      return;
    case Qt::Key_Up:
      emit pixelNudgeRequested(0, -1);
      event->accept();
      return;
    case Qt::Key_Down:
      emit pixelNudgeRequested(0, 1);
      event->accept();
      return;
    case Qt::Key_Escape:
      emit selectionCancelled();
      event->accept();
      return;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
}

}  // namespace pnga::ui::qt
