// WP-5U3A Image interaction tests. The widget owns only presentation state;
// pixel hover is transient while click/keyboard events are explicit signals.

#include <pnga/ui/qt/delivered_image_view.h>

#include <QtTest/QtTest>

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QImage>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QToolButton>
#include <QWheelEvent>
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
  void zoomControlsSynchronizeButtonsWheelAndEditablePercentage();

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

  // Showing a mouse-tracking widget can generate an initial hover at the
  // system cursor position. Reset that presentation state so the test starts
  // from a deterministic pointer state.
  view.clearHoverPixel();
  const QPoint hovered_point(150, 50);
  QMouseEvent hover_event(
      QEvent::MouseMove, QPointF(hovered_point),
      QPointF(view.mapToGlobal(hovered_point)), Qt::NoButton, Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(&view, &hover_event);
  QVERIFY(hovered.count() >= 1);
  QCOMPARE(hovered.last().at(0).toInt(), 1);
  QCOMPARE(hovered.last().at(1).toInt(), 0);
  QEvent leave_event(QEvent::Leave);
  QApplication::sendEvent(&view, &leave_event);
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

void DeliveredImageViewInteractionTest::zoomControlsSynchronizeButtonsWheelAndEditablePercentage() {
  pnga::ui::qt::DeliveredImageView view;
  view.resize(400, 300);
  view.setImage(QImage(100, 100, QImage::Format_RGBA8888));
  view.show();
  QCoreApplication::processEvents();

  auto* zoom_out =
      view.findChild<QToolButton*>(QStringLiteral("imageZoomOut"));
  auto* zoom_percent =
      view.findChild<QComboBox*>(QStringLiteral("imageZoomPercent"));
  auto* zoom_in =
      view.findChild<QToolButton*>(QStringLiteral("imageZoomIn"));
  QVERIFY(zoom_out != nullptr);
  QVERIFY(zoom_percent != nullptr);
  QVERIFY(zoom_in != nullptr);
  QVERIFY(zoom_percent->isEditable());
  QCOMPARE(zoom_percent->count(), 7);
  QCOMPARE(zoom_percent->itemText(0), QStringLiteral("25%"));
  QCOMPARE(zoom_percent->itemText(6), QStringLiteral("200%"));
  QCOMPARE(qRound(view.zoomPercent()), 300);
  QCOMPARE(zoom_percent->currentText(), QStringLiteral("300%"));

  QTest::mouseClick(zoom_percent, Qt::LeftButton, Qt::NoModifier,
                    QPoint(zoom_percent->width() - 6,
                           zoom_percent->height() / 2));
  QTRY_VERIFY(zoom_percent->view()->isVisible());
  QTest::keyClick(zoom_percent->view(), Qt::Key_Escape);

  QTest::mouseClick(zoom_in, Qt::LeftButton);
  QCOMPARE(qRound(view.zoomPercent()), 375);
  QCOMPARE(zoom_percent->currentText(), QStringLiteral("375%"));
  QTest::mouseClick(zoom_out, Qt::LeftButton);
  QCOMPARE(qRound(view.zoomPercent()), 300);

  const QPoint anchor(200, 150);
  QWheelEvent wheel_event(
      QPointF(anchor), QPointF(view.mapToGlobal(anchor)), QPoint(),
      QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&view, &wheel_event);
  QCOMPARE(qRound(view.zoomPercent()), 375);
  QCOMPARE(zoom_percent->currentText(), QStringLiteral("375%"));

  auto* editor = zoom_percent->lineEdit();
  QVERIFY(editor != nullptr);
  editor->setFocus();
  editor->selectAll();
  QTest::keyClicks(editor, QStringLiteral("150%"));
  QTest::keyClick(editor, Qt::Key_Return);
  QCOMPARE(qRound(view.zoomPercent()), 150);
  QCOMPARE(zoom_percent->currentText(), QStringLiteral("150%"));

  // Choosing an entry from the list uses the same visible percentage
  // immediately, rather than only changing the editable text.
  QVERIFY(QMetaObject::invokeMethod(
      zoom_percent, "textActivated", Qt::DirectConnection,
      Q_ARG(QString, QStringLiteral("200%"))));
  QCOMPARE(qRound(view.zoomPercent()), 200);
  QCOMPARE(zoom_percent->currentText(), QStringLiteral("200%"));
}

QTEST_MAIN(DeliveredImageViewInteractionTest)
#include "delivered_image_view_interaction_test.moc"
