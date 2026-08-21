// WP-204 ImageTransform: zoom-about-center pan/zoom mapping.

#include "pnga/ui/qt/image_transform.h"

#include <algorithm>
#include <cmath>

namespace pnga::ui::qt {

namespace {

double clamp_zoom(double zoom) { return std::clamp(zoom, 0.01, 64.0); }

}  // namespace

void ImageTransform::fit(const QSizeF& viewport, const QSizeF& image) noexcept {
  viewport_ = viewport;
  image_ = image;
  if (image_.isEmpty() || viewport_.isEmpty()) {
    zoom_ = 1.0;
    offset_ = QPointF();
    return;
  }
  zoom_ = std::min(viewport_.width() / image_.width(),
                   viewport_.height() / image_.height());
  zoom_ = clamp_zoom(zoom_);
  offset_ = QPointF((viewport_.width() - image_.width() * zoom_) / 2.0,
                    (viewport_.height() - image_.height() * zoom_) / 2.0);
}

void ImageTransform::centerOn(const QPointF& imagePoint,
                              const QSizeF& viewport) noexcept {
  viewport_ = viewport;
  offset_ = QPointF(viewport_.width() / 2.0 - imagePoint.x() * zoom_,
                    viewport_.height() / 2.0 - imagePoint.y() * zoom_);
}

void ImageTransform::zoomBy(double factor, const QPointF& viewportAnchor) noexcept {
  if (image_.isEmpty()) {
    return;
  }
  const QPointF imagePoint = widgetToImage(viewportAnchor);
  zoom_ = clamp_zoom(zoom_ * factor);
  // Keep the image point under the anchor fixed.
  offset_ = QPointF(viewportAnchor.x() - imagePoint.x() * zoom_,
                    viewportAnchor.y() - imagePoint.y() * zoom_);
}

void ImageTransform::panBy(const QPointF& delta) noexcept {
  offset_ += delta;
}

QPointF ImageTransform::imageToWidget(const QPointF& imagePoint) const noexcept {
  return QPointF(offset_.x() + imagePoint.x() * zoom_,
                 offset_.y() + imagePoint.y() * zoom_);
}

QPointF ImageTransform::widgetToImage(const QPointF& widgetPoint) const noexcept {
  if (image_.isEmpty() || zoom_ == 0.0) {
    return QPointF();
  }
  const double x = (widgetPoint.x() - offset_.x()) / zoom_;
  const double y = (widgetPoint.y() - offset_.y()) / zoom_;
  return QPointF(std::clamp(x, 0.0, image_.width() - 1.0),
                 std::clamp(y, 0.0, image_.height() - 1.0));
}

}  // namespace pnga::ui::qt
