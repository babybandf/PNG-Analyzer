// WP-5U12 responsive and accessibility gates for the Compression pages. The
// pages must honor 320/360/480/600 logical-pixel body widths without a
// content-driven minimum width, keep the master table scrolling inside its
// viewport, and expose stable accessible names for tables, buttons and the
// shared context.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QAbstractScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

class CompressionInspectorResponsiveTest : public QObject {
  Q_OBJECT
 private slots:
  void pagesHonorNarrowWidthsWithoutGrowth();
  void accessibleNamesArePresent();
  void detailsRemainAvailable();
  void replacingDetailsDoesNotLeaveOverlappingLabels();
};

static pnga::analysis_engine::BlockInspectorView ready_blocks() {
  pnga::analysis_engine::BlockInspectorView view;
  view.status = pnga::analysis_engine::BlockInspectorStatus::kReady;
  view.scanline = 3;
  view.selected_output_offset = 5;
  pnga::analysis_engine::BlockInspectorRow row;
  row.block_index = 1;
  row.type = pnga::deflate_index::BlockType::kDynamic;
  row.last = true;
  row.input_bit_begin = 786;
  row.input_bit_end = 1206;
  row.output_begin = 1536;
  row.output_end = 3104;
  row.current_output_position = 1573;
  row.physical_spans.push_back(
      {pnga::trace_model::ProvenanceSpace::kPhysicalFile, 67, 54, 0, 0,
       false});
  view.rows.push_back(row);
  return view;
}

static pnga::analysis_engine::DecodeTraceInspectorView ready_decode() {
  pnga::analysis_engine::DecodeTraceInspectorView view;
  view.status = pnga::analysis_engine::DecodeTraceInspectorStatus::kReady;
  view.selected_token_index = 35;
  view.selected_output_offset = 1573;
  pnga::analysis_engine::DecodeTraceStep step;
  step.token_index = 35;
  step.path = pnga::analysis_engine::DecodeTracePath::kMatch;
  step.input_bit_begin = 922;
  step.input_bit_end = 937;
  step.output_begin = 1569;
  step.output_end = 1587;
  step.selected = true;
  step.selected_output_byte = 1573;
  step.match_source_ranges.push_back({1562, 1580, 34});
  view.steps.push_back(step);
  return view;
}

void CompressionInspectorResponsiveTest::pagesHonorNarrowWidthsWithoutGrowth() {
  const int widths[] = {320, 360, 480, 600};
  for (const int width : widths) {
    pnga::ui::qt::BlockInspector block;
    pnga::ui::qt::HuffmanInspector huffman;
    pnga::ui::qt::DecodeTraceInspector decode;
    block.setView(ready_blocks());
    decode.setView(ready_decode());
    QWidget* pages[] = {&block, &huffman, &decode};
    for (QWidget* page : pages) {
      page->setFixedWidth(width);
      page->show();
      QCoreApplication::processEvents();
      QCOMPARE(page->width(), width);
      QVERIFY2(page->minimumWidth() <= width,
               qPrintable(QStringLiteral("minimum width %1 exceeds %2")
                              .arg(page->minimumWidth())
                              .arg(width)));
    }
    auto* block_table = block.findChild<QTableWidget*>(
        QStringLiteral("blockInspectorTable"));
    QVERIFY(block_table != nullptr);
    QVERIFY(block_table->width() <= width);
    QVERIFY(block_table->viewport()->width() > 0);
  }
}

void CompressionInspectorResponsiveTest::accessibleNamesArePresent() {
  pnga::ui::qt::BlockInspector block;
  pnga::ui::qt::HuffmanInspector huffman;
  pnga::ui::qt::DecodeTraceInspector decode;
  block.setView(ready_blocks());
  decode.setView(ready_decode());

  QVERIFY(!block.findChild<QTableWidget*>(
               QStringLiteral("blockInspectorTable"))
               ->accessibleName()
               .isEmpty());
  QVERIFY(!huffman.findChild<QTableWidget*>(
               QStringLiteral("huffmanInspectorTable"))
               ->accessibleName()
               .isEmpty());
  QVERIFY(!decode.findChild<QTableWidget*>(
               QStringLiteral("decodeTraceInspectorTable"))
               ->accessibleName()
               .isEmpty());

  bool found = false;
  const auto buttons = block.findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      found = !button->accessibleName().isEmpty() ||
              !button->objectName().isEmpty();
      QVERIFY(button->objectName() == QStringLiteral("blockShowInHex"));
    }
  }
  QVERIFY(found);

  pnga::ui::qt::CompressionContext context;
  QVERIFY(context.statusLabel()->objectName() ==
          QStringLiteral("compressionContextStatus"));
  QVERIFY(context.mappingLabel()->objectName() ==
          QStringLiteral("compressionContextMapping"));
}

void CompressionInspectorResponsiveTest::detailsRemainAvailable() {
  pnga::ui::qt::BlockInspector block;
  block.setView(ready_blocks());
  auto* details_title =
      block.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #1")));
  // The details body still renders the copied span text.
  bool found = false;
  const auto labels = block.findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->text() == QStringLiteral("file[67..121)")) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void CompressionInspectorResponsiveTest::replacingDetailsDoesNotLeaveOverlappingLabels() {
  pnga::ui::qt::BlockInspector block;
  auto first = ready_blocks();
  block.setView(first);

  auto second = ready_blocks();
  second.rows.front().block_index = 2;
  second.rows.front().physical_spans.front().offset = 200;
  second.rows.front().physical_spans.front().length = 10;
  block.setView(second);

  auto* details_body =
      block.findChild<QWidget*>(QStringLiteral("compressionDetailsBody"));
  QVERIFY(details_body != nullptr);

  const auto labels = details_body->findChildren<QLabel*>(
      QString(), Qt::FindDirectChildrenOnly);
  // Seven label/value rows belong to the current Block details only. Old rows
  // must be deleted synchronously rather than lingering until the event loop.
  QCOMPARE(labels.size(), 14);

  bool found_current_span = false;
  for (const auto* label : labels) {
    QVERIFY(label->text() != QStringLiteral("file[67..121)"));
    if (label->text() == QStringLiteral("file[200..210)")) {
      found_current_span = true;
    }
  }
  QVERIFY(found_current_span);
}

QTEST_MAIN(CompressionInspectorResponsiveTest)
#include "compression_inspector_responsive_test.moc"
