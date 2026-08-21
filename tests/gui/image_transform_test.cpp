// WP-204 ImageTransform test: fit, zoom-about-anchor and pixel round-trips.

#include <pnga/ui/qt/image_transform.h>

#include <QtTest/QtTest>

#include <cmath>

using pnga::ui::qt::ImageTransform;

namespace {

bool near(qreal a, qreal b, qreal eps = 1e-9) { return std::abs(a - b) < eps; }

}  // namespace

class ImageTransformTest : public QObject {
  Q_OBJECT
 private slots:
  void fitScalesToViewport();
  void pixelRoundTripUnderZoom();
  void zoomAboutAnchorKeepsImagePointFixed();
  void panShiftsTheOffset();
};

void ImageTransformTest::fitScalesToViewport() {
  ImageTransform t;
  t.fit(QSizeF(800, 600), QSizeF(400, 200));
  // zoom = min(800/400, 600/200) = min(2, 3) = 2; centered.
  QVERIFY(near(t.zoom(), 2.0));
  QVERIFY(near(t.offset().x(), (800 - 400 * 2.0) / 2.0));
  QVERIFY(near(t.offset().y(), (600 - 200 * 2.0) / 2.0));
}

void ImageTransformTest::pixelRoundTripUnderZoom() {
  ImageTransform t;
  t.fit(QSizeF(800, 600), QSizeF(100, 100));
  t.zoomBy(4.0, QPointF(400, 300));  // zoom about the viewport center
  const QPointF p = t.imageToWidget(QPointF(50, 50));
  const QPointF back = t.widgetToImage(p);
  QVERIFY(near(back.x(), 50.0, 1e-6));
  QVERIFY(near(back.y(), 50.0, 1e-6));
}

void ImageTransformTest::zoomAboutAnchorKeepsImagePointFixed() {
  ImageTransform t;
  t.fit(QSizeF(800, 600), QSizeF(400, 400));
  const QPointF anchor(200, 150);
  const QPointF before = t.widgetToImage(anchor);
  t.zoomBy(2.0, anchor);
  const QPointF after = t.imageToWidget(before);
  QVERIFY(near(after.x(), anchor.x(), 1e-6));
  QVERIFY(near(after.y(), anchor.y(), 1e-6));
}

void ImageTransformTest::panShiftsTheOffset() {
  ImageTransform t;
  t.fit(QSizeF(800, 600), QSizeF(400, 400));
  const QPointF before = t.imageToWidget(QPointF(0, 0));
  t.panBy(QPointF(10, -5));
  const QPointF after = t.imageToWidget(QPointF(0, 0));
  QVERIFY(near(after.x() - before.x(), 10.0));
  QVERIFY(near(after.y() - before.y(), -5.0));
}

QTEST_MAIN(ImageTransformTest)
#include "image_transform_test.moc"
