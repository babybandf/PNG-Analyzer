// WP-5U12 responsive and accessibility gates for the Compression pages. The
// pages must honor 320/360/480/600 logical-pixel body widths without a
// content-driven minimum width, keep the master table scrolling inside its
// viewport, expose the normative Blocks column matrix, footer order, row and
// header geometry, the 55:45 master/details split and stable accessible
// names for tables, buttons and the shared context.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/block_inspector_model.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QAbstractScrollArea>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableView>

#include <cstdint>
#include <memory>

namespace {

pnga::png_format::ChunkNode idat(std::uint64_t offset,
                                 std::uint64_t length) {
  pnga::png_format::ChunkNode node;
  node.data_offset = offset;
  node.data_length = length;
  node.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
               std::byte{'T'}};
  return node;
}

// Stored [0,3) + Fixed [3,9) + Dynamic [9,12) over two IDAT segments.
pnga::analysis_engine::FastCompressionIndexView readyIndex(
    std::uint64_t generation, std::uint64_t base = 100) {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(base, 3), idat(base + 100, 4)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 16;
  index.total_output_bytes = 12;
  index.adler.status = pnga::deflate_index::Adler32Status::kMatch;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                          8, 24, 0, 3});
  index.blocks.push_back({1, pnga::deflate_index::BlockType::kFixed, false,
                          24, 40, 3, 9});
  index.blocks.push_back({2, pnga::deflate_index::BlockType::kDynamic, true,
                          40, 56, 9, 12});
  return pnga::analysis_engine::build_fast_compression_index(
      generation, index, stream);
}

pnga::analysis_engine::DecodeTraceInspectorView ready_decode() {
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

QTableView* blocksTable(pnga::ui::qt::BlockInspector& widget) {
  return widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
}

}  // namespace

class CompressionInspectorResponsiveTest : public QObject {
  Q_OBJECT
 private slots:
  void pagesHonorNarrowWidthsWithoutGrowth();
  void blocksColumnsFollowWidthMatrix();
  void blocksGeometryFooterAndSplitter();
  void currentAndSelectionCoexistAtAnyWidth();
  void accessibleNamesArePresent();
  void detailsRemainAvailable();
  void replacingDetailsDoesNotLeaveOverlappingLabels();
};

void CompressionInspectorResponsiveTest::pagesHonorNarrowWidthsWithoutGrowth() {
  const int widths[] = {320, 360, 480, 600};
  for (const int width : widths) {
    pnga::ui::qt::BlockInspector block;
    block.setFastIndex(readyIndex(3));
    pnga::ui::qt::HuffmanInspector huffman;
    pnga::ui::qt::DecodeTraceInspector decode;
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
    auto* table = blocksTable(block);
    QVERIFY(table != nullptr);
    QVERIFY(table->width() <= width);
    QVERIFY(table->viewport()->width() > 0);
  }
}

void CompressionInspectorResponsiveTest::blocksColumnsFollowWidthMatrix() {
  struct WidthCase {
    int width;
    bool scanlines;
    bool events;
  };
  const WidthCase cases[] = {
      {600, true, true}, {480, false, true}, {360, false, false},
      {320, false, false},
  };
  for (const auto& width_case : cases) {
    pnga::ui::qt::BlockInspector block;
    block.setFastIndex(readyIndex(3));
    block.setFixedWidth(width_case.width);
    block.show();
    QCoreApplication::processEvents();
    auto* table = blocksTable(block);
    QVERIFY(table != nullptr);
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Current),
             false);
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Number),
             false);
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Type),
             false);
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Final),
             false);
    QCOMPARE(
        table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::InputBits),
        false);
    QCOMPARE(
        table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::OutputBytes),
        false);
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Events),
             !width_case.events);
    QCOMPARE(
        table->isColumnHidden(pnga::ui::qt::BlockInspectorModel::Scanlines),
        !width_case.scanlines);
    if (!width_case.events) {
      // Narrow widths scroll inside the viewport instead of growing.
      QVERIFY(table->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
      QVERIFY(table->minimumWidth() <= width_case.width);
    }
  }
}

void CompressionInspectorResponsiveTest::blocksGeometryFooterAndSplitter() {
  pnga::ui::qt::BlockInspector block;
  block.setFastIndex(readyIndex(3));
  block.setFixedWidth(600);
  block.resize(600, 600);
  block.show();
  QVERIFY(QTest::qWaitForWindowExposed(&block));
  QCoreApplication::processEvents();
  auto* table = blocksTable(block);
  QVERIFY(table != nullptr);
  // Normative geometry bands (flow-ui §20.3).
  QVERIFY2(table->verticalHeader()->defaultSectionSize() >= 26 &&
               table->verticalHeader()->defaultSectionSize() <= 32,
           qPrintable(QStringLiteral("row height %1 outside 26..32")
                          .arg(table->verticalHeader()
                                   ->defaultSectionSize())));
  QVERIFY2(table->horizontalHeader()->height() >= 26 &&
               table->horizontalHeader()->height() <= 31,
           qPrintable(QStringLiteral("header height %1 outside 26..31")
                          .arg(table->horizontalHeader()->height())));

  // Footer actions keep the normative order: Show in Hex, then Show
  // inflated output; Open Decode Trace lives in the details area below.
  auto* hex_button = block.findChild<QPushButton*>(
      QStringLiteral("blockShowInHex"));
  auto* inflated_button = block.findChild<QPushButton*>(
      QStringLiteral("blockShowInflatedOutput"));
  auto* trace_button = block.findChild<QPushButton*>(
      QStringLiteral("blockOpenDecodeTrace"));
  QVERIFY(hex_button != nullptr);
  QVERIFY(inflated_button != nullptr);
  QVERIFY(trace_button != nullptr);
  QCOMPARE(hex_button->text(), QStringLiteral("Show in Hex"));
  QCOMPARE(inflated_button->text(), QStringLiteral("Show inflated output"));
  QCOMPARE(trace_button->text(), QStringLiteral("Open Decode Trace"));
  QVERIFY(hex_button->x() < inflated_button->x());
  // The footer action bar is the page's bottom row; the Open Decode Trace
  // drill-in lives inside the details pane above it (flow-ui §6.1, §7.2).
  QVERIFY(trace_button->mapTo(&block, QPoint(0, 0)).y() <
          hex_button->mapTo(&block, QPoint(0, 0)).y());

  // The master/details split stays close to the normative 55:45 with both
  // panes usable.
  auto* splitter = block.findChild<QSplitter*>(
      QStringLiteral("compressionPageSplitter"));
  QVERIFY(splitter != nullptr);
  const int table_height = splitter->sizes().value(0);
  const int details_height = splitter->sizes().value(1);
  QVERIFY(table_height >= 4 * 28);  // at least four table rows visible
  QVERIFY(details_height >= 120);
  const double ratio =
      static_cast<double>(table_height) /
      static_cast<double>(table_height + details_height);
  QVERIFY2(ratio >= 0.50 && ratio <= 0.62,
           qPrintable(QStringLiteral("split ratio %1 outside 0.50..0.62")
                          .arg(ratio)));
}

void CompressionInspectorResponsiveTest::currentAndSelectionCoexistAtAnyWidth() {
  pnga::ui::qt::BlockInspector block;
  block.setFastIndex(readyIndex(3));
  block.setFixedWidth(360);
  block.show();
  QCoreApplication::processEvents();
  auto* table = blocksTable(block);
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);

  pnga::trace_model::CompressionSelectionState state;
  state.generation = 3;
  pnga::trace_model::CompressionCurrentMapping current;
  current.generation = 3;
  current.source_unit = pnga::trace_model::DocumentSourceUnit{};
  current.output_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{1},
      pnga::trace_model::InflatedByteOffset{2}};
  current.block_index = 0;
  state.current = current;
  pnga::trace_model::CompressionNavigationTarget manual;
  manual.generation = 3;
  manual.request_serial = 1;
  manual.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  manual.logical_range = pnga::trace_model::ZlibBitRange{
      pnga::trace_model::ZlibBitOffset{40},
      pnga::trace_model::ZlibBitOffset{56}};
  manual.physical_spans = {pnga::trace_model::FileByteRange{
      pnga::trace_model::FileByteOffset{202},
      pnga::trace_model::FileByteOffset{204}}};
  manual.block_index = 2;
  state.manual = manual;
  block.setSelectionState(state);

  // Current icon and native selection stay simultaneously visible, also in
  // the narrow width band.
  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::ContainsCurrentRole)
              .toBool());
  QCOMPARE(model->data(model->index(0, 0), Qt::DisplayRole).toString(),
           QStringLiteral("●"));
  table->selectRow(2);
  QVERIFY(table->selectionModel()->isRowSelected(2, QModelIndex()));
  QVERIFY(model->data(model->index(2, 0),
                      pnga::ui::qt::IsManualSelectionRole)
              .toBool());
  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::ContainsCurrentRole)
              .toBool());
}

void CompressionInspectorResponsiveTest::accessibleNamesArePresent() {
  pnga::ui::qt::BlockInspector block;
  block.setFastIndex(readyIndex(3));
  pnga::ui::qt::HuffmanInspector huffman;
  pnga::ui::qt::DecodeTraceInspector decode;
  decode.setView(ready_decode());

  QVERIFY(!blocksTable(block)->accessibleName().isEmpty());
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
  block.setFastIndex(readyIndex(3));
  auto* table = blocksTable(block);
  QVERIFY(table != nullptr);
  table->selectRow(0);
  auto* details_title =
      block.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #0")));
  // The details body still renders the copied span text.
  bool found = false;
  const auto labels = block.findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->text() == QStringLiteral("file[101..103)")) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void CompressionInspectorResponsiveTest::replacingDetailsDoesNotLeaveOverlappingLabels() {
  pnga::ui::qt::BlockInspector block;
  block.setFastIndex(readyIndex(3));
  auto* table = blocksTable(block);
  QVERIFY(table != nullptr);
  table->selectRow(0);

  // A different Fast Index regenerates the details grid synchronously; the
  // old rows must be deleted rather than lingering until the event loop.
  block.setFastIndex(readyIndex(4, 300));
  table->selectRow(0);

  auto* details_body =
      block.findChild<QWidget*>(QStringLiteral("compressionDetailsBody"));
  QVERIFY(details_body != nullptr);

  const auto labels = details_body->findChildren<QLabel*>(
      QString(), Qt::FindDirectChildrenOnly);
  // Eleven label/value rows belong to the current Block details only.
  QCOMPARE(labels.size(), 22);

  bool found_stale_span = false;
  bool found_current_span = false;
  for (const auto* label : labels) {
    if (label->text() == QStringLiteral("file[101..103)")) {
      found_stale_span = true;
    }
    if (label->text() == QStringLiteral("file[301..303)")) {
      found_current_span = true;
    }
  }
  QVERIFY(!found_stale_span);
  QVERIFY(found_current_span);
}

QTEST_MAIN(CompressionInspectorResponsiveTest)
#include "compression_inspector_responsive_test.moc"
