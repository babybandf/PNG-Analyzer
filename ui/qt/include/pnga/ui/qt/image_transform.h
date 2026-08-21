#ifndef PNGA_UI_QT_IMAGE_TRANSFORM_H
#define PNGA_UI_QT_IMAGE_TRANSFORM_H

// WP-204: viewport transform for the delivered image. Maps image pixel
// coordinates to widget coordinates (and back) under zoom about a center
// point. Pure math, no Qt painting, so it is unit-testable.

#include <QPointF>
#include <QRectF>

namespace pnga::ui::qt {

class ImageTransform {
 public:
  // Zoom level: 1.0 fits the image into the viewport; larger zooms in.
  void fit(const QSizeF& viewport, const QSizeF& image) noexcept;

  double zoom() const noexcept { return zoom_; }
  QPointF offset() const noexcept { return offset_; }  // widget pos of image origin

  // Center the view on `imagePoint` at the current zoom.
  void centerOn(const QPointF& imagePoint, const QSizeF& viewport) noexcept;

  void zoomBy(double factor, const QPointF& viewportAnchor) noexcept;
  void panBy(const QPointF& delta) noexcept;

  // image pixel -> widget pixel
  QPointF imageToWidget(const QPointF& imagePoint) const noexcept;
  // widget pixel -> image pixel (clamped to the image bounds)
  QPointF widgetToImage(const QPointF& widgetPoint) const noexcept;

  QSizeF imageSize() const noexcept { return image_; }
  QSizeF viewportSize() const noexcept { return viewport_; }

 private:
  QSizeF viewport_;
  QSizeF image_;
  double zoom_ = 1.0;
  QPointF offset_;  // widget coordinate of image pixel (0,0)
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_IMAGE_TRANSFORM_H
