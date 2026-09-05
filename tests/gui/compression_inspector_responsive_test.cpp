// WP-5U12 responsive and accessibility gates for the Compression pages. The
// pages must honor 320/360/480/600 logical-pixel body widths without a
// content-driven minimum width, keep the master table scrolling inside its
// viewport, expose the normative Blocks column matrix, footer order, row and
// header geometry, the 55:45 master/details split and stable accessible
// names for tables, buttons and the shared context. WP-5U12E adds the Decode
// Trace width/state matrix: all five columns at every width, the Event
// interactive width, long facts in the details, Loading/Empty/Partial/Error copy,
// Current+Selection coexistence, Light/Dark theme tokens and the footer
// action order.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/block_inspector_model.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_model.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QAbstractScrollArea>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QScrollBar>
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
  using pnga::analysis_engine::DecodeTracePath;
  using pnga::analysis_engine::DecodeTraceStep;
  using pnga::analysis_engine::TraceQueryStatus;
  using pnga::trace_model::DeflateBitOffset;
  using pnga::trace_model::DeflateBitRange;
  using pnga::trace_model::FileByteOffset;
  using pnga::trace_model::FileByteRange;
  using pnga::trace_model::InflatedByteOffset;
  using pnga::trace_model::InflatedByteRange;

  pnga::analysis_engine::DecodeTraceInspectorView view;
  view.scope.generation = 3;
  view.scope.requested_output = InflatedByteRange{InflatedByteOffset{1568},
                                                  InflatedByteOffset{1587}};
  view.scope.status = TraceQueryStatus::kReady;
  view.scope.returned_token_count = 3;

  DecodeTraceStep literal;
  literal.token_index = 35;
  literal.block_index = 0;
  literal.path = DecodeTracePath::kLiteral;
  literal.input_range = DeflateBitRange{DeflateBitOffset{918},
                                        DeflateBitOffset{922}};
  literal.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{43}, FileByteOffset{44}});
  literal.output_range = InflatedByteRange{InflatedByteOffset{1568},
                                           InflatedByteOffset{1569}};
  literal.event_text = "Literal 0x41";
  literal.huffman_symbol = 65;
  view.steps.push_back(literal);

  DecodeTraceStep match;
  match.token_index = 36;
  match.block_index = 0;
  match.path = DecodeTracePath::kMatch;
  match.input_range = DeflateBitRange{DeflateBitOffset{922},
                                      DeflateBitOffset{937}};
  // The token input crosses an IDAT boundary: two ordered file spans.
  match.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{43}, FileByteOffset{44}});
  match.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{56}, FileByteOffset{57}});
  match.output_range = InflatedByteRange{InflatedByteOffset{1569},
                                         InflatedByteOffset{1587}};
  match.event_text = "Match len 18 / dist 7";
  match.huffman_symbol = 268;
  match.length = 18;
  match.distance = 7;
  match.length_base = 17;
  match.length_extra_bits = 1;
  match.length_extra_value = 1;
  match.distance_base = 7;
  match.distance_extra_bits = 1;
  match.distance_extra_value = 0;
  match.match_source_ranges.push_back({1562, 1580, 34});
  match.match_target = match.output_range;
  match.match_overlaps = true;
  match.contains_current = true;
  match.selected_byte_offset_in_event = 4;
  view.steps.push_back(match);

  DecodeTraceStep eob;
  eob.token_index = 37;
  eob.block_index = 0;
  eob.path = DecodeTracePath::kEndOfBlock;
  eob.input_range = DeflateBitRange{DeflateBitOffset{937},
                                    DeflateBitOffset{944}};
  eob.event_text = "End of block";
  eob.huffman_symbol = 256;
  view.steps.push_back(eob);
  return view;
}

QTableView* blocksTable(pnga::ui::qt::BlockInspector& widget) {
  return widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
}

// Dynamic block #7 with a literal/length and a distance table; the fixture
// carries the projection's own bit strings so no Qt code reverses bits.
pnga::analysis_engine::HuffmanInspectorView ready_huffman() {
  pnga::analysis_engine::HuffmanInspectorView view;
  view.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  view.generation = 3;
  pnga::analysis_engine::HuffmanInspectorTable literal;
  literal.block_index = 7;
  literal.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  literal.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  literal.selector_label = "Literal / Length";
  literal.build_order = 1;
  literal.declared_entry_count = 3;
  literal.bounded_token_count = 3;
  pnga::analysis_engine::HuffmanInspectorEntry e65;
  e65.symbol = 65;
  e65.bit_length = 3;
  e65.canonical_code = 4;
  e65.read_order_code = 1;
  e65.meaning = "literal 65";
  e65.canonical_bits = "100";
  e65.read_order_bits = "001";
  e65.occurrence_token_indices = {3};
  literal.entries.push_back(e65);
  pnga::analysis_engine::HuffmanInspectorEntry e268;
  e268.symbol = 268;
  e268.bit_length = 3;
  e268.canonical_code = 5;
  e268.read_order_code = 5;
  e268.meaning = "length 17-18";
  e268.canonical_bits = "101";
  e268.read_order_bits = "101";
  e268.occurrence_token_indices = {4};
  literal.entries.push_back(e268);
  pnga::analysis_engine::HuffmanInspectorEntry e256;
  e256.symbol = 256;
  e256.bit_length = 1;
  e256.meaning = "end-of-block";
  e256.canonical_bits = "0";
  e256.read_order_bits = "0";
  e256.occurrence_token_indices = {5};
  literal.entries.push_back(e256);
  view.tables.push_back(literal);
  pnga::analysis_engine::HuffmanInspectorTable distance;
  distance.block_index = 7;
  distance.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  distance.kind = pnga::deflate_trace::HuffmanTableKind::kDistance;
  distance.selector_label = "Distance";
  distance.build_order = 2;
  distance.declared_entry_count = 1;
  distance.bounded_token_count = 3;
  pnga::analysis_engine::HuffmanInspectorEntry d0;
  d0.symbol = 0;
  d0.bit_length = 1;
  d0.meaning = "distance 1";
  d0.canonical_bits = "0";
  d0.read_order_bits = "0";
  distance.entries.push_back(d0);
  view.tables.push_back(distance);
  pnga::analysis_engine::HuffmanBlockScope scope;
  scope.block_index = 7;
  scope.deflate_range = pnga::trace_model::DeflateBitRange{
      pnga::trace_model::DeflateBitOffset{0},
      pnga::trace_model::DeflateBitOffset{64}};
  scope.physical_spans.push_back(
      pnga::trace_model::ProvenanceSpan{
          pnga::trace_model::ProvenanceSpace::kPhysicalFile, 100, 10, 0, 80,
          true});
  view.block_scopes.push_back(scope);
  return view;
}

}  // namespace

class CompressionInspectorResponsiveTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void pagesHonorNarrowWidthsWithoutGrowth();
  void blocksColumnsFollowWidthMatrix();
  void huffmanColumnsFollowWidthMatrix();
  void decodeColumnsFollowWidthMatrix();
  void blocksGeometryFooterAndSplitter();
  void decodeGeometryFooterAndSplitter();
  void decodeStatesCopyAndTheme();
  void currentAndSelectionCoexistAtAnyWidth();
  void accessibleNamesArePresent();
  void detailsRemainAvailable();
  void replacingDetailsDoesNotLeaveOverlappingLabels();
  void contentColumnsSupportDragAndRefit_data();
  void contentColumnsSupportDragAndRefit();
};

void CompressionInspectorResponsiveTest::contentColumnsSupportDragAndRefit_data() {
  QTest::addColumn<int>("page");
  QTest::addColumn<int>("column");
  QTest::addColumn<int>("width");
  QTest::newRow("blocks-input") << 0 << 4 << 360;
  QTest::newRow("blocks-last-visible") << 0 << 5 << 360;
  QTest::newRow("blocks-last") << 0 << 7 << 600;
  QTest::newRow("huffman-meaning") << 1 << 1 << 360;
  QTest::newRow("huffman-last") << 1 << 5 << 360;
  QTest::newRow("decode-event") << 2 << 3 << 360;
  QTest::newRow("decode-last") << 2 << 4 << 360;
}

void CompressionInspectorResponsiveTest::contentColumnsSupportDragAndRefit() {
  QFETCH(int, page);
  QFETCH(int, column);
  QFETCH(int, width);
  pnga::ui::qt::BlockInspector blocks;
  pnga::ui::qt::HuffmanInspector huffman;
  pnga::ui::qt::DecodeTraceInspector decode;
  auto publish = [&] {
    if (page == 0) blocks.setFastIndex(readyIndex(3));
    if (page == 1) huffman.setView(ready_huffman());
    if (page == 2) decode.setView(ready_decode());
  };
  QWidget* widget = page == 0 ? static_cast<QWidget*>(&blocks)
                    : page == 1 ? static_cast<QWidget*>(&huffman) : &decode;
  widget->setFixedWidth(width);
  widget->resize(width, 600);
  publish();
  widget->show();
  QCoreApplication::processEvents();
  auto* table = widget->findChild<QTableView*>();
  QVERIFY(table != nullptr);
  auto* header = table->horizontalHeader();
  table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  // Leave room to drag even the final section boundary inside the viewport.
  table->setColumnWidth(column, 80);
  QCoreApplication::processEvents();
  table->horizontalScrollBar()->setValue(
      header->sectionPosition(column) + header->sectionSize(column) - 160);
  QCoreApplication::processEvents();
  const auto boundary = [&] {
    return QPoint(header->sectionViewportPosition(column) +
                      header->sectionSize(column) - 1,
                  header->height() / 2);
  };
  const int before = table->columnWidth(column);
  const QPoint start = boundary();
  QVERIFY(start.x() > 0 && start.x() < header->viewport()->width());
  QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(header->viewport(), start + QPoint(100, 0));
  QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                      start + QPoint(100, 0));
  QCOMPARE(table->columnWidth(column), before + 100);
  if (width == 360) {
    // QTableView updates scroll ranges on the deferred layout pass.
    QTRY_VERIFY(table->horizontalScrollBar()->maximum() > 0);
  }
  QCOMPARE(widget->width(), width);
  const int manual_width = table->columnWidth(column);
  publish();
  table->selectRow(0);
  widget->hide();
  widget->show();
  QCoreApplication::processEvents();
  QCOMPARE(table->columnWidth(column), manual_width);
  // Compare with a plain Qt table's independent fit using the same model.
  QTableView reference;
  reference.setModel(table->model());
  reference.resizeColumnToContents(column);
  table->horizontalScrollBar()->setValue(
      header->sectionPosition(column) + header->sectionSize(column) - 160);
  QCoreApplication::processEvents();
  QTest::mouseDClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                    boundary());
  QCOMPARE(table->columnWidth(column), reference.columnWidth(column));
  if (page != 1) {
    QCOMPARE(table->columnWidth(0), 28);
    QCOMPARE(header->sectionResizeMode(0), QHeaderView::Fixed);
  }
}

void CompressionInspectorResponsiveTest::initTestCase() {
  // The Current-row background uses the centralized theme token; the theme
  // is installed (without persisting settings) so the color tokens resolve
  // on the offscreen platform as well.
  auto* theme = new pnga::ui::qt::ApplicationTheme(qApp, this);
  theme->setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                 /*persist=*/false);
}

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

void CompressionInspectorResponsiveTest::huffmanColumnsFollowWidthMatrix() {
  // Normative Huffman behavior (flow-ui §20.4/§20.5): the six required
  // columns stay present at every width; Canonical and Uses in result live
  // in the viewport's horizontal scroll on narrow pages, and the selected
  // row's details keep both bit orders.
  for (const int width : {600, 480, 360, 320}) {
    pnga::ui::qt::HuffmanInspector huffman;
    huffman.setView(ready_huffman());
    huffman.setFixedWidth(width);
    huffman.show();
    QCoreApplication::processEvents();
    QCOMPARE(huffman.width(), width);
    QVERIFY2(huffman.minimumWidth() <= width,
             qPrintable(QStringLiteral("Huffman minimum width %1 exceeds %2")
                            .arg(huffman.minimumWidth())
                            .arg(width)));
    auto* table = huffman.findChild<QTableView*>(
        QStringLiteral("compressionHuffmanTable"));
    QVERIFY(table != nullptr);
    for (int column = 0;
         column < pnga::ui::qt::HuffmanInspectorModel::ColumnCount;
         ++column) {
      QVERIFY2(!table->isColumnHidden(column),
               qPrintable(QStringLiteral("column %1 hidden at %2 px")
                              .arg(column)
                              .arg(width)));
    }
    QVERIFY(table->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
    if (width == 360) {
      table->selectRow(0);
      bool found_canonical = false;
      bool found_read_order = false;
      const auto labels = huffman.findChildren<QLabel*>();
      for (const auto* label : labels) {
        if (label->text().contains(QStringLiteral("100 · 3 bits"))) {
          found_canonical = true;
        }
        if (label->text().contains(QStringLiteral("001"))) {
          found_read_order = true;
        }
      }
      QVERIFY(found_canonical);
      QVERIFY(found_read_order);
    }
  }
}

void CompressionInspectorResponsiveTest::decodeColumnsFollowWidthMatrix() {
  // Normative Decode Trace behavior (flow-ui §20.4/§20.5): Current | Step |
  // Input bits | Event | Output stay present at every width, the Event
  // column supports manual resizing and long facts stay in the details;
  // narrow pages scroll horizontally inside the viewport instead of growing
  // the Inspector minimum width.
  for (const int width : {600, 480, 360, 320}) {
    pnga::ui::qt::DecodeTraceInspector decode;
    decode.setView(ready_decode());
    decode.setFixedWidth(width);
    decode.show();
    QCoreApplication::processEvents();
    QCOMPARE(decode.width(), width);
    QVERIFY2(decode.minimumWidth() <= width,
             qPrintable(QStringLiteral("Decode Trace minimum width %1 exceeds "
                                       "%2")
                            .arg(decode.minimumWidth())
                            .arg(width)));
    auto* table = decode.findChild<QTableView*>(
        QStringLiteral("compressionDecodeTraceTable"));
    QVERIFY(table != nullptr);
    for (int column = 0;
         column < pnga::ui::qt::DecodeTraceModel::ColumnCount; ++column) {
      QVERIFY2(!table->isColumnHidden(column),
               qPrintable(QStringLiteral("column %1 hidden at %2 px")
                              .arg(column)
                              .arg(width)));
    }
    QVERIFY(table->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(
                 pnga::ui::qt::DecodeTraceModel::Event),
             QHeaderView::Interactive);
    QVERIFY(table->viewport()->width() > 0);
    if (width == 320) {
      // Long facts stay in the details while the table scrolls internally.
      table->selectRow(1);
      bool found_length = false;
      bool found_overlap = false;
      const auto labels = decode.findChildren<QLabel*>();
      for (const auto* label : labels) {
        if (label->text().contains(QStringLiteral("base 17 + extra 1"))) {
          found_length = true;
        }
        if (label->text() == QStringLiteral("yes")) {
          found_overlap = true;
        }
      }
      QVERIFY(found_length);
      QVERIFY(found_overlap);
    }
  }
}

void CompressionInspectorResponsiveTest::decodeGeometryFooterAndSplitter() {
  pnga::ui::qt::DecodeTraceInspector decode;
  decode.setView(ready_decode());
  decode.setFixedWidth(600);
  decode.resize(600, 600);
  decode.show();
  QVERIFY(QTest::qWaitForWindowExposed(&decode));
  QCoreApplication::processEvents();
  auto* table = decode.findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
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

  // Footer actions keep the normative order and locked labels.
  auto* hex_button = decode.findChild<QPushButton*>(
      QStringLiteral("decodeShowInHex"));
  auto* inflated_button = decode.findChild<QPushButton*>(
      QStringLiteral("decodeShowInflatedOutput"));
  QVERIFY(hex_button != nullptr);
  QVERIFY(inflated_button != nullptr);
  QCOMPARE(hex_button->text(), QStringLiteral("Show in Hex"));
  QCOMPARE(inflated_button->text(), QStringLiteral("Show inflated output"));
  QVERIFY(hex_button->x() < inflated_button->x());

  // The master/details split stays close to the normative 55:45 with both
  // panes usable.
  auto* splitter = decode.findChild<QSplitter*>(
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

  // Action enablement follows the active step's typed payload: the match
  // row enables both actions; the end-of-block row in this fixture has no
  // physical input spans and no output bytes, so both actions stay disabled
  // (no span, no navigation).
  table->selectRow(1);
  QVERIFY(hex_button->isEnabled());
  QVERIFY(inflated_button->isEnabled());
  table->selectRow(2);
  QVERIFY(!hex_button->isEnabled());
  QVERIFY(!inflated_button->isEnabled());
}

void CompressionInspectorResponsiveTest::decodeStatesCopyAndTheme() {
  pnga::ui::qt::DecodeTraceInspector decode;
  decode.setView(ready_decode());
  decode.setFixedWidth(360);
  decode.show();
  QCoreApplication::processEvents();

  // Partial keeps the verified rows and names the stop reason; the scope
  // heading carries the bounded title facts (never `no trace`).
  auto partial = ready_decode();
  partial.scope.status =
      pnga::analysis_engine::TraceQueryStatus::kPartial;
  partial.scope.truncated = true;
  partial.scope.stop_reason = "trace token budget exceeded";
  decode.setView(partial);
  auto* heading = decode.findChild<QLabel*>(
      QStringLiteral("decodeTraceScopeHeading"));
  QVERIFY(heading != nullptr);
  QVERIFY(heading->text().contains(QStringLiteral("partial")));
  QVERIFY(heading->text().contains(QStringLiteral("truncated")));
  QVERIFY(heading->text().contains(
      QStringLiteral("trace token budget exceeded")));
  QVERIFY(heading->text().contains(QStringLiteral("output bytes 1568–1587")));
  QVERIFY(!heading->text().contains(QStringLiteral("no trace")));
  QCOMPARE(decode.findChild<QTableView*>(
               QStringLiteral("compressionDecodeTraceTable"))
               ->model()
               ->rowCount(),
           3);

  // An error result keeps its verified rows and names the stop reason.
  auto error = ready_decode();
  error.scope.status = pnga::analysis_engine::TraceQueryStatus::kError;
  error.scope.stop_reason = "invalid distance code";
  decode.setView(error);
  bool found_stop = false;
  for (const auto* label : decode.findChildren<QLabel*>()) {
    if (label->text().contains(QStringLiteral("invalid distance code"))) {
      found_stop = true;
    }
  }
  QVERIFY(found_stop);

  // Loading keeps the published facts and shows the analyzing copy instead
  // of an empty table or `no trace`.
  auto loading = ready_decode();
  loading.scope.status =
      pnga::analysis_engine::TraceQueryStatus::kReplaying;
  decode.setView(loading);
  QVERIFY(heading->text().contains(QStringLiteral("replaying")));

  // The empty bounded result uses the normative human text.
  pnga::ui::qt::DecodeTraceInspector empty;
  empty.setView(pnga::analysis_engine::DecodeTraceInspectorView{});
  empty.show();
  QCoreApplication::processEvents();
  bool found_empty_copy = false;
  for (const auto* label : empty.findChildren<QLabel*>()) {
    if (label->text().contains(QStringLiteral("No tokens in the bounded"))) {
      found_empty_copy = true;
    }
  }
  QVERIFY(found_empty_copy);

  // Light and Dark resolve the Current marker through the centralized theme
  // token, never a hard-coded RGB literal.
  auto* table = decode.findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  pnga::trace_model::CompressionSelectionState state;
  state.generation = 3;
  pnga::trace_model::CompressionCurrentMapping current;
  current.generation = 3;
  current.source_unit = pnga::trace_model::DocumentSourceUnit{};
  current.output_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{1573},
      pnga::trace_model::InflatedByteOffset{1574}};
  current.block_index = 0;
  state.current = current;
  decode.setSelectionState(state);
  QVERIFY(model->data(model->index(1, 0),
                      pnga::ui::qt::DecodeTraceContainsCurrentRole)
              .toBool());
  pnga::ui::qt::ApplicationTheme theme(qApp);
  theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kDark,
                /*persist=*/false);
  const QColor dark_current =
      model->data(model->index(1, 1), Qt::BackgroundRole).value<QColor>();
  QCOMPARE(dark_current,
           pnga::ui::qt::ApplicationTheme::applicationColor(
               pnga::ui::qt::ApplicationTheme::ColorToken::kCurrentPixel));
  theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                /*persist=*/false);
  const QColor light_current =
      model->data(model->index(1, 1), Qt::BackgroundRole).value<QColor>();
  QCOMPARE(light_current,
           pnga::ui::qt::ApplicationTheme::applicationColor(
               pnga::ui::qt::ApplicationTheme::ColorToken::kCurrentPixel));
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
  QVERIFY(!huffman.findChild<QTableView*>(
                 QStringLiteral("compressionHuffmanTable"))
                 ->accessibleName()
                 .isEmpty());
  QVERIFY(!decode.findChild<QTableView*>(
                QStringLiteral("compressionDecodeTraceTable"))
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
