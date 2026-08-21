#ifndef PNGA_UI_QT_DELIVERED_IMAGE_VIEW_H
#define PNGA_UI_QT_DELIVERED_IMAGE_VIEW_H

// WP-204: renders the delivered RGBA image with zoom/pan and publishes pixel
// selections. The widget is Qt-only: it receives a QImage (the app converts
// backend output off the UI thread) and never touches libpng or decode logic.

#include "pnga/ui/qt/image_transform.h"

#include <QImage>
#include <QPoint>
#include <QWidget>

namespace pnga::ui::qt {

class DeliveredImageView final : public QWidget {
  Q_OBJECT
 public:
  explicit DeliveredImageView(QWidget* parent = nullptr);

  // `image` is delivered on the UI thread only (decoding happens off-thread).
  void setImage(const QImage& image);

  QImage image() const { return image_; }

  // Reads the RGBA value at image pixel (x, y), or null when out of bounds.
  std::optional<std::array<std::uint8_t, 4>> rgbaAt(int x, int y) const;

  // For tests: maps a widget point back to image pixels at current zoom.
  QPoint imagePointAt(const QPoint& widgetPoint) const;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 signals:
  // Published when the user clicks a pixel (image coordinates).
  void pixelSelected(int x, int y);

 private:
  void refit();

  QImage image_;
  ImageTransform transform_;
  QPoint lastMouse_;
  bool panning_ = false;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_DELIVERED_IMAGE_VIEW_H
