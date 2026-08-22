// WP-5U3A Image interaction tests. The widget owns only presentation state;
// pixel hover is transient while click/keyboard events are explicit signals.

#include <pnga/ui/qt/delivered_image_view.h>

#include <QtTest/QtTest>

#include <QImage>
#include <QSignalSpy>
#include <QWidget>

class DeliveredImageViewInteractionTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void imagePixelMappingUsesPixelBounds();
  void clickPublishesPixelOnlyWithoutDrag();
  void dragPansWithoutPublishingSelection();
  void hoverPublishesAndClearsWithoutSelection();
  void keyboardPublishesNudgeAndCancel();

 private:
  QImage image_;
};

void DeliveredImageViewInteractionTest::init() {
  image_ = QImage(2, 2, QImage::Format_RGBA8888);
  image_.fill(qRgba(10, 20, 30, 255));
}

void DeliveredImageViewInteractionTest::imagePixelMappingUsesPixelBounds() {
  QWidget host;
  host.resize(300, 200);
  pnga::ui::qt::DeliveredImageView view;
  view.setParent(&host);
  view.setGeometry(host.rect());
  view.setImage(QImage(3, 2, QImage::Format_RGBA8888));
  host.show();
  QCoreApplication::processEvents();

  QCOMPARE(view.imagePixelAt(QPoint(60, 40)),
           std::optional<QPoint>(QPoint(0, 0)));
  QCOMPARE(view.imagePixelAt(QPoint(120, 40)),
           std::optional<QPoint>(QPoint(1, 0)));
  QVERIFY(!view.imagePixelAt(QPoint(0, 0)).has_value());
}

void DeliveredImageViewInteractionTest::clickPublishesPixelOnlyWithoutDrag() {
  pnga::ui::qt::DeliveredImageView view;
  view.resize(200, 200);
  view.setImage(image_);
  view.show();
  QCoreApplication::processEvents();
  QSignalSpy selected(&view,
                     &pnga::ui::qt::DeliveredImageView::pixelSelected);
  QSignalSpy hovered(&view,
                     &pnga::ui::qt::DeliveredImageView::pixelHovered);

  QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.at(0).at(0).toInt(), 0);
  QCOMPARE(selected.at(0).at(1).toInt(), 0);
  QVERIFY(hovered.count() <= 1);
}

void DeliveredImageViewInteractionTest::dragPansWithoutPublishingSelection() {
  pnga::ui::qt::DeliveredImageView view;
  view.resize(200, 200);
  view.setImage(image_);
  view.show();
  QCoreApplication::processEvents();
  QSignalSpy selected(&view,
                     &pnga::ui::qt::DeliveredImageView::pixelSelected);

  QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
  QTest::mouseMove(&view, QPoint(80, 80));
  QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
  QCOMPARE(selected.count(), 0);
}

void DeliveredImageViewInteractionTest::hoverPublishesAndClearsWithoutSelection() {
  pnga::ui::qt::DeliveredImageView view;
  view.resize(200, 200);
  view.setImage(image_);
  view.show();
  QCoreApplication::processEvents();
  QSignalSpy hovered(&view,
                     &pnga::ui::qt::DeliveredImageView::pixelHovered);
  QSignalSpy left(&view, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft);
  QSignalSpy selected(&view,
                      &pnga::ui::qt::DeliveredImageView::pixelSelected);

  QTest::mouseMove(&view, QPoint(150, 50));
  QVERIFY(hovered.count() >= 1);
  QCOMPARE(hovered.last().at(0).toInt(), 1);
  QCOMPARE(hovered.last().at(1).toInt(), 0);
  QTest::mouseMove(&view, QPoint(-1, -1));
  QVERIFY(left.count() >= 1);
  QCOMPARE(selected.count(), 0);
}

void DeliveredImageViewInteractionTest::keyboardPublishesNudgeAndCancel() {
  pnga::ui::qt::DeliveredImageView view;
  view.resize(200, 200);
  view.setImage(image_);
  view.show();
  view.setFocus();
  QCoreApplication::processEvents();
  QSignalSpy nudge(
      &view, &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested);
  QSignalSpy cancelled(&view,
                       &pnga::ui::qt::DeliveredImageView::selectionCancelled);

  QTest::keyClick(&view, Qt::Key_Right);
  QTest::keyClick(&view, Qt::Key_Down);
  QTest::keyClick(&view, Qt::Key_Escape);
  QCOMPARE(nudge.count(), 2);
  QCOMPARE(nudge.at(0).at(0).toInt(), 1);
  QCOMPARE(nudge.at(0).at(1).toInt(), 0);
  QCOMPARE(nudge.at(1).at(0).toInt(), 0);
  QCOMPARE(nudge.at(1).at(1).toInt(), 1);
  QCOMPARE(cancelled.count(), 1);
}

QTEST_MAIN(DeliveredImageViewInteractionTest)
#include "delivered_image_view_interaction_test.moc"
