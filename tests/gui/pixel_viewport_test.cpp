// WP-5U3B PixelViewport consumer test. The view receives an immutable
// StageSet and displays only the bounded native-sample window around a center.

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/ui/qt/pixel_viewport.h>

#include <QtTest/QtTest>

#include <QLabel>

#include <memory>

class PixelViewportTest : public QObject {
  Q_OBJECT
 private slots:
  void nativeWindowTextFollowsCenter();
};

void PixelViewportTest::nativeWindowTextFollowsCenter() {
  auto stages = std::make_shared<pnga::analysis_engine::StageSet>();
  stages->success = true;
  stages->native.width = 3;
  stages->native.height = 2;
  stages->native.channels = 2;
  stages->native.samples = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  pnga::ui::qt::PixelViewport view;
  view.setStageSet(stages);
  auto* label = view.findChild<QLabel*>();
  QVERIFY(label != nullptr);
  QVERIFY(label->text().contains(QStringLiteral("Native samples")));
  QVERIFY(label->text().contains(QStringLiteral("(0,0): [1, 2]")));

  view.setCenter(2, 1);
  QVERIFY(label->text().contains(QStringLiteral("window x=1 y=0")));
  QVERIFY(label->text().contains(QStringLiteral("(2,1): [11, 12]")));
}

QTEST_MAIN(PixelViewportTest)
#include "pixel_viewport_test.moc"
