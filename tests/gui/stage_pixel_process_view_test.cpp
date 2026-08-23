// WP-5U9 shared stage explanation widget contract.

#include <pnga/io/byte_source.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>

#include <QtTest/QtTest>

#include <QTextEdit>

#include <memory>

#include "../common/test_png_helpers.h"

class StagePixelProcessViewTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersEachStageWithSharedNeighborhoodContract();
};

void StagePixelProcessViewTest::rendersEachStageWithSharedNeighborhoodContract() {
  const auto encoded = pnga_test::encode_png(5, 4, 8, 2, false, true);
  auto source = std::make_shared<pnga::io::MemoryByteSource>(encoded.png_bytes);
  auto stages = std::make_shared<const pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source));
  QVERIFY(stages->success);

  pnga::ui::qt::StagePixelProcessView pixels(
      pnga::analysis_engine::StagePixelProcessStage::kNative);
  pnga::ui::qt::StagePixelProcessView filtered(
      pnga::analysis_engine::StagePixelProcessStage::kFiltered);
  pnga::ui::qt::StagePixelProcessView defiltered(
      pnga::analysis_engine::StagePixelProcessStage::kDefiltered);
  for (auto* view : {&pixels, &filtered, &defiltered}) {
    view->setStageSet(stages);
    view->setCoordinate(2, 2);
    auto* text = view->findChild<QTextEdit*>();
    QVERIFY(text != nullptr);
    QVERIFY(text->isReadOnly());
    QVERIFY(text->acceptRichText());
    QVERIFY(text->toHtml().contains(QStringLiteral("<table")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("coordinate=(2, 2)")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("current")));
    QVERIFY(!text->toPlainText().contains(QStringLiteral("CURRENT:")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Current value calculation")));
  }
  auto* filtered_text = filtered.findChild<QTextEdit*>();
  QVERIFY(filtered_text->toPlainText().contains(QStringLiteral("Inflate output")));
  auto* defiltered_text = defiltered.findChild<QTextEdit*>();
  QVERIFY(defiltered_text->toPlainText().contains(QStringLiteral("predictor")));

  pixels.setNumericBase(true);
  QVERIFY(pixels.findChild<QTextEdit*>()->toPlainText().contains(
      QStringLiteral("Native pixels (HEX)")));
}

QTEST_MAIN(StagePixelProcessViewTest)
#include "stage_pixel_process_view_test.moc"
