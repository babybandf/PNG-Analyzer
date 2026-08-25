// WP-306 stage inspector model tests: stage switching at a fixed pixel keeps
// row meaning (coordinate consistency), per-channel values match the pipeline
// stages, and the filter formula text carries the expected a/b/c/predictor.

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/ui/qt/stage_inspector_model.h>
#include <pnga/ui/qt/stage_inspector.h>

#include <QtTest/QtTest>

#include <QTextEdit>

#include <cstdint>
#include <memory>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::analysis_engine::StageSet;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using pnga::trace_model::Stage;
using pnga::ui::qt::StageInspectorModel;

namespace {

std::shared_ptr<const StageSet> stages_of(const EncodedPng& e) {
  auto source = std::make_shared<MemoryByteSource>(e.png_bytes);
  const ChunkIndex index = pnga::png_format::index_chunks(*source);
  VirtualIDATStream stream(index);
  return std::make_shared<StageSet>(
      pnga::analysis_engine::analyze_stages(stream, *source, e.header));
}

}  // namespace

class StageInspectorModelTest : public QObject {
  Q_OBJECT
 private slots:
  void stageSwitchKeepsRows();
  void nativeValuesMatchPipeline();
  void filteredAndUnfilteredBytes();
  void formulaTextCarriesNeighbors();
  void deliveredUsesProvidedPixels();
  void outOfBoundsIsEmpty();
  void reconstructReportUsesViewModel();
  void pixelNeighborhoodShowsPaethCDependency();
  void filterReportUsesActualDependencyRoles();
};

void StageInspectorModelTest::stageSwitchKeepsRows() {
  // RGBA8 8x8: 4 channels -> 4 rows regardless of the stage.
  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  QVERIFY(model.hasData());
  QCOMPARE(model.rowCount(), 4);
  QCOMPARE(model.columnCount(), StageInspectorModel::kColumnCount);

  model.setPixel(3, 2);
  QCOMPARE(model.rowCount(), 4);  // coordinate consistency
  QCOMPARE(model.pixelX(), std::uint64_t{3});
  QCOMPARE(model.pixelY(), std::uint64_t{2});

  model.setStage(Stage::kNative);
  QCOMPARE(model.rowCount(), 4);  // switching stage keeps the row meaning
  model.setStage(Stage::kUnfiltered);
  QCOMPARE(model.rowCount(), 4);
  model.setStage(Stage::kFiltered);
  QCOMPARE(model.rowCount(), 4);
}

void StageInspectorModelTest::nativeValuesMatchPipeline() {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  model.setPixel(3, 2);
  model.setStage(Stage::kNative);

  // Channel 2 of pixel (3, 2): sample = raw byte at y*32 + (x*4+2).
  const std::uint64_t off = 2 * 32 + (3 * 4 + 2);
  const int want = static_cast<int>(static_cast<std::uint8_t>(e.raw[off]));
  const QString text =
      model.data(model.index(2, StageInspectorModel::kValue)).toString();
  QCOMPARE(text.toInt(), want);
}

void StageInspectorModelTest::filteredAndUnfilteredBytes() {
  // Sub-byte gray 2-bit, width 8: pixel (5, 1) channel 0 is a 2-bit sample.
  const EncodedPng e = encode_png(8, 8, 2, 0, false, true);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  model.setPixel(5, 1);
  model.setStage(Stage::kUnfiltered);

  // Unfiltered == raw for all-None encoding; row 1 starts at byte 2 and the
  // 2-bit sample at x=5 sits at bit offset 10 within the row.
  const std::uint8_t value = test_read_bits(e.raw.data() + 2, 10, 2);
  const QString text =
      model.data(model.index(0, StageInspectorModel::kValue)).toString();
  QCOMPARE(text.toInt(nullptr, 16), static_cast<int>(value));

  // Filtered stage reads the same bits from the flat filtered stream.
  model.setStage(Stage::kFiltered);
  const QString ftext =
      model.data(model.index(0, StageInspectorModel::kValue)).toString();
  QCOMPARE(ftext.toInt(nullptr, 16), static_cast<int>(value));
}

void StageInspectorModelTest::formulaTextCarriesNeighbors() {
  // Row 4 of a rotating-filter RGBA8 image is Paeth (filter_for(4)).
  const EncodedPng e = encode_png(8, 8, 8, 6, false, false);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  model.setPixel(1, 4);
  const auto text = model.formulaText(4);
  QVERIFY(text.has_value());
  QVERIFY(text->contains(QLatin1String("row 4 byte 4")));
  QVERIFY(text->contains(QLatin1String("paeth")));  // filter_type_text is lowercase
  QVERIFY(text->contains(QLatin1String("recon=")));
  // recon of byte 4 must equal the source byte y=4, x=1, ch=0.
  const int want = static_cast<int>(static_cast<std::uint8_t>(e.raw[4 * 32 + 4]));
  QVERIFY(text->contains(QStringLiteral("recon=%1").arg(want)));
}

void StageInspectorModelTest::deliveredUsesProvidedPixels() {
  const EncodedPng e = encode_png(4, 4, 8, 6, false, true);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  // 4x4 RGBA8 delivered buffer with a known value at pixel (1, 2), channel 3.
  std::vector<std::byte> rgba(4 * 4 * 4, std::byte{0x00});
  rgba[(2 * 4 + 1) * 4 + 3] = std::byte{0x2A};
  model.setDeliveredPixels(4, 4, std::move(rgba));
  model.setPixel(1, 2);
  model.setStage(Stage::kDelivered);
  const QString text =
      model.data(model.index(3, StageInspectorModel::kValue)).toString();
  QCOMPARE(text.toInt(), 0x2A);
}

void StageInspectorModelTest::outOfBoundsIsEmpty() {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  StageInspectorModel model;
  model.setStageSet(stages_of(e));
  model.setPixel(100, 100);
  model.setStage(Stage::kNative);
  QCOMPARE(model.rowCount(), 4);  // rows still reported
  // Values read as the "—" placeholder, not garbage.
  const QString text =
      model.data(model.index(0, StageInspectorModel::kValue)).toString();
  QCOMPARE(text, QStringLiteral("—"));
}

void StageInspectorModelTest::reconstructReportUsesViewModel() {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, false);
  pnga::ui::qt::StageInspector inspector;
  inspector.setStageSet(stages_of(e));
  inspector.onPixelSelected(1, 4);
  auto* report =
      inspector.findChild<QTextEdit*>(QStringLiteral("reconstructReport"));
  QVERIFY(report != nullptr);
  QCOMPARE(report->document()->documentMargin(), 8.0);
  const QString text = report->toPlainText();
  QVERIFY(text.contains(QStringLiteral("Target pixel")));
  QVERIFY(text.contains(QStringLiteral("Scanline location")));
  QVERIFY(text.contains(QStringLiteral("Filtered data")));
  QVERIFY(!text.contains(QStringLiteral("raw filtered X:")));
  QVERIFY(text.contains(QStringLiteral("Pixel neighborhood")));
  QVERIFY(!text.contains(QStringLiteral("y-1")));
  QVERIFY(!text.contains(QStringLiteral("y (current)")));
  QVERIFY(!text.contains(QStringLiteral("Filter / predictor / bounds")));
  QVERIFY(text.contains(QStringLiteral("Per-channel reconstruction")));
  QVERIFY(!text.contains(QStringLiteral("Neighbors")));
  QVERIFY(text.contains(QStringLiteral("Predictor:")));
  QVERIFY(text.contains(QStringLiteral("Recon:")));
  QVERIFY(report->toHtml().contains(QStringLiteral("Step")));
  QVERIFY(text.contains(QStringLiteral("mod 256")));
  QVERIFY(report->toHtml().contains(QStringLiteral("FFF4CC"),
                                    Qt::CaseInsensitive));
  QVERIFY(text.contains(QStringLiteral("Final RGBA")));
  QVERIFY(!text.contains(QStringLiteral("row query:")));
  inspector.setNumericBase(true);
  QVERIFY(report->toPlainText().contains(QStringLiteral("0x")));
}

void StageInspectorModelTest::pixelNeighborhoodShowsPaethCDependency() {
  // Row 4 is Paeth in the rotating-filter fixture.  For pixel (1, 4), c is
  // the up-left pixel (0, 3) and must remain a visible, labeled grid cell.
  const EncodedPng e = encode_png(8, 8, 8, 6, false, false);
  pnga::ui::qt::StageInspector inspector;
  inspector.setStageSet(stages_of(e));
  inspector.onPixelSelected(1, 4);
  auto* report = inspector.findChild<QTextEdit*>(QStringLiteral("reconstructReport"));
  QVERIFY(report != nullptr);
  const QString text = report->toPlainText();
  const QString neighborhood =
      text.section(QStringLiteral("Pixel neighborhood"), 1, 1)
          .section(QStringLiteral("Per-channel reconstruction"), 0, 0);
  QVERIFY(neighborhood.contains(QStringLiteral("23\nc\n26\nb")));
}

void StageInspectorModelTest::filterReportUsesActualDependencyRoles() {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, false);
  pnga::ui::qt::StageInspector inspector;
  inspector.setStageSet(stages_of(e));
  auto* report = inspector.findChild<QTextEdit*>(QStringLiteral("reconstructReport"));
  QVERIFY(report != nullptr);
  const QStringList filters{QStringLiteral("none"), QStringLiteral("sub"),
                            QStringLiteral("up"), QStringLiteral("average"),
                            QStringLiteral("paeth")};
  for (int row = 0; row < filters.size(); ++row) {
    inspector.onPixelSelected(1, static_cast<std::uint64_t>(row));
    const QString text = report->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("filter: %1").arg(filters.at(row))),
             qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("current")));
  }
}

QTEST_MAIN(StageInspectorModelTest)
#include "stage_inspector_test.moc"
