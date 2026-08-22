// WP-5U3C adaptive preview status tests.

#include <pnga/ui/qt/stage_preview_view.h>

#include <QtTest/QtTest>

#include <memory>

class StagePreviewViewTest : public QObject {
  Q_OBJECT
 private slots:
  void conditionsAreExplicitForPackedAdam7PaletteAlpha();
};

void StagePreviewViewTest::conditionsAreExplicitForPackedAdam7PaletteAlpha() {
  auto stages = std::make_shared<pnga::analysis_engine::StageSet>();
  stages->success = true;
  stages->header.width = 4;
  stages->header.height = 4;
  stages->header.bit_depth = 4;
  stages->header.color_type = 3;
  stages->header.interlace = true;
  stages->interlace = true;
  stages->scanlines.resize(7);
  stages->filtered.resize(21);
  stages->unfiltered.resize(20);

  pnga::ui::qt::StagePreviewView view(
      pnga::ui::qt::PreviewStage::kFiltered);
  view.setStageSet(stages);
  view.setCoordinate(2, 3);
  const QString summary = view.summary();
  QVERIFY(summary.contains(QStringLiteral("Filtered bytes")));
  QVERIFY(summary.contains(QStringLiteral("Adam7")));
  QVERIFY(summary.contains(QStringLiteral("Palette index")));
  QVERIFY(summary.contains(QStringLiteral("Packed samples")));
  QVERIFY(summary.contains(QStringLiteral("coordinate: (2, 3)")));
  QVERIFY(summary.contains(QStringLiteral("pass-major")));
}

QTEST_MAIN(StagePreviewViewTest)
#include "stage_preview_view_test.moc"
