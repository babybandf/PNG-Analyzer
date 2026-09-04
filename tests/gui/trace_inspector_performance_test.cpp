// WP-5U12F Task 4: the Compression inspector performance gate, in its final
// form. The three product pages are model-backed QTableViews, so the
// capped-QTableWidget-era assertions are replaced with the model contract:
//
//   - the Blocks model publishes the complete 10,000-row Fast Index fact;
//   - the Huffman model publishes the maximum bounded table (288 entries,
//     the RFC 1951 section 3.2.6 fixed literal/length cardinality);
//   - the Decode Trace model publishes exactly the 4,096-token bounded
//     budget (WP-5U13 kMaxTraceTokens);
//   - every table is a QTableView over QAbstractItemModel with zero row
//     widgets, before and after scrolling;
//   - visible rows format on demand through a deterministic 200-scroll
//     read pass;
//   - set-model stays within the carried-forward cold/hot thresholds;
//   - the pages retain exactly the published immutable projections — typed
//     ranges only, no retained duplicate token/output buffers.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/hex_data_source.h>
#include <pnga/io/byte_source.h>

#include <QtTest/QtTest>

#include <QAbstractItemView>
#include <QColor>
#include <QElapsedTimer>
#include <QTableView>
#include <QTableWidget>

#include <string>

#include <cstdint>

namespace {

// WP-5U13 bounded Deep Trace budget: no Decode Trace publication can exceed
// this row count, so the gate pins the model to exactly it.
constexpr int kBoundedTraceTokens = 4096;
// RFC 1951 section 3.2.6: the predefined literal/length table is the largest
// possible Huffman table; the gate pins the model to exactly it.
constexpr int kMaxHuffmanTableEntries = 288;
// Fast Index stress size carried forward at equal strength.
constexpr int kLargeBlockRows = 10000;
constexpr int kScrollTargets = 200;
// Reviewed fixed maxima. Basis: measured on the development machine
// (Apple Silicon, offscreen Qt) — the full set-model pass takes a few
// milliseconds and the full 200-scroll pass across all three tables takes
// ~31 ms; the set-model ceiling keeps the 2000 ms value the migration rounds
// already used, the scroll ceiling is 1000 ms. Both fail the target
// regression mode outright: formatting every row of a table on each scroll
// target would cost seconds at these scales.
constexpr qint64 kSetModelMaxMs = 2000;
constexpr qint64 kScrollPassMaxMs = 1000;

// Deterministic scroll/row sequence, the same fixed convention the
// performance corpus runner uses (no clock, no randomness).
int scroll_row(int i, int rows) {
  return static_cast<int>((static_cast<std::uint64_t>(i) * 2654435761ull +
                           17ull) % static_cast<std::uint64_t>(rows));
}

pnga::analysis_engine::FastCompressionIndexView large_fast_index() {
  pnga::analysis_engine::FastCompressionIndexView view;
  view.status = pnga::analysis_engine::FastCompressionIndexStatus::kReady;
  view.generation = 1;
  for (std::uint64_t i = 0; i < kLargeBlockRows; ++i) {
    pnga::analysis_engine::FastCompressionBlockRow row;
    row.block_index = i;
    row.type = pnga::deflate_index::BlockType::kStored;
    row.last = i + 1 == kLargeBlockRows;
    row.input_range = {pnga::trace_model::ZlibBitOffset{8ull + 64ull * i},
                       pnga::trace_model::ZlibBitOffset{8ull + 64ull * (i + 1)}};
    row.output_range = {pnga::trace_model::InflatedByteOffset{i},
                        pnga::trace_model::InflatedByteOffset{i + 1}};
    view.blocks.push_back(row);
  }
  return view;
}

pnga::analysis_engine::HuffmanInspectorView max_huffman_view() {
  pnga::analysis_engine::HuffmanInspectorView view;
  view.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  view.generation = 1;
  pnga::analysis_engine::HuffmanInspectorTable table;
  table.mode = pnga::analysis_engine::HuffmanTableMode::kFixed;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  table.declared_entry_count = kMaxHuffmanTableEntries;
  // RFC 1951 section 3.2.6 predefined code lengths: the exact maximum
  // bounded table shape the production projection emits for Fixed blocks.
  for (std::uint16_t symbol = 0; symbol < kMaxHuffmanTableEntries; ++symbol) {
    pnga::analysis_engine::HuffmanInspectorEntry entry;
    entry.symbol = symbol;
    entry.meaning = symbol <= 255
                        ? "literal " + std::to_string(symbol)
                        : (symbol == 256 ? "end of block"
                                         : "length " + std::to_string(symbol));
    entry.bit_length = symbol <= 143 ? 8
                       : symbol <= 255 ? 9
                       : symbol <= 279 ? 7
                                       : 8;
    entry.canonical_code = symbol;
    entry.read_order_code = symbol;
    entry.canonical_bits = std::string(entry.bit_length, '0');
    entry.read_order_bits = std::string(entry.bit_length, '1');
    // Fixed entries are predefined and carry no provenance range.
    table.entries.push_back(entry);
  }
  view.tables.push_back(table);
  return view;
}

pnga::analysis_engine::DecodeTraceInspectorView bounded_decode_view() {
  pnga::analysis_engine::DecodeTraceInspectorView view;
  view.scope.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  view.scope.generation = 1;
  view.scope.requested_output = {
      pnga::trace_model::InflatedByteOffset{0},
      pnga::trace_model::InflatedByteOffset{kBoundedTraceTokens}};
  view.scope.returned_token_count = kBoundedTraceTokens;
  for (std::uint64_t i = 0; i < kBoundedTraceTokens; ++i) {
    pnga::analysis_engine::DecodeTraceStep step;
    step.token_index = i;
    step.block_index = 0;
    step.path = pnga::analysis_engine::DecodeTracePath::kLiteral;
    step.input_range = {pnga::trace_model::DeflateBitOffset{8ull * i},
                        pnga::trace_model::DeflateBitOffset{8ull * (i + 1)}};
    step.output_range = {pnga::trace_model::InflatedByteOffset{i},
                         pnga::trace_model::InflatedByteOffset{i + 1}};
    step.event_text = "Literal 0x" + std::to_string(i % 256);
    view.steps.push_back(step);
  }
  return view;
}

// Model/view contract: a QTableView over a QAbstractItemModel with zero row
// widgets — no QTableWidget and no per-row index widget in the viewport.
void assert_model_view(QTableView* table) {
  QVERIFY(table != nullptr);
  QVERIFY(qobject_cast<QTableWidget*>(table) == nullptr);
  QVERIFY(table->model() != nullptr);
  QVERIFY(table->model()->columnCount() > 0);
  const auto index_widgets = table->viewport()->findChildren<QWidget*>(
      QString(), Qt::FindDirectChildrenOnly);
  QVERIFY2(index_widgets.isEmpty(), "table viewport holds per-row widgets");
}

// One visible-row read: every column formats on demand; value columns must
// be non-empty (the leading Current marker is empty unless the row carries
// the current byte). Accumulates a deterministic checksum.
void read_visible_row(QAbstractItemModel* model, int row, int columns,
                      int first_value_column, quint64* checksum) {
  for (int column = 0; column < columns; ++column) {
    const QVariant value = model->data(model->index(row, column),
                                       Qt::DisplayRole);
    QVERIFY(value.isValid());
    const QString text = value.toString();
    if (column >= first_value_column) {
      QVERIFY2(!text.isEmpty(), qPrintable(QStringLiteral(
                                    "empty formatted cell r%1 c%2")
                                    .arg(row)
                                    .arg(column)));
    }
    *checksum += static_cast<quint64>(text.size());
  }
}

// Deterministic 200-scroll response pass: a fixed sequence of scroll targets
// per table, each formatted on demand; no clock, no randomness.
void scroll_read_pass(QTableView* view, int rows, int columns,
                      int first_value_column, qint64* elapsed_ms,
                      quint64* checksum) {
  QVERIFY(view != nullptr);
  auto* model = view->model();
  QVERIFY(model != nullptr);
  QElapsedTimer timer;
  timer.start();
  for (int i = 0; i < kScrollTargets; ++i) {
    const int row = scroll_row(i, rows);
    view->scrollTo(model->index(row, 0), QAbstractItemView::PositionAtCenter);
    read_visible_row(model, row, columns, first_value_column, checksum);
  }
  *elapsed_ms = timer.elapsed();
}

}  // namespace

class TraceInspectorPerformanceTest : public QObject {
  Q_OBJECT
 private slots:
  void largeModelsPublishCompleteRowsFast();
  void hexHighlightsAreCapped();
};

void TraceInspectorPerformanceTest::largeModelsPublishCompleteRowsFast() {
  const auto fast_blocks = large_fast_index();
  const auto huffman = max_huffman_view();
  const auto decode = bounded_decode_view();

  pnga::ui::qt::BlockInspector block_widget;
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;

  // Set-model cold/hot thresholds (carried forward from the migration
  // rounds at equal strength).
  QElapsedTimer timer;
  timer.start();
  block_widget.setFastIndex(fast_blocks);
  huffman_widget.setView(huffman);
  decode_widget.setView(decode);
  const qint64 cold_ms = timer.elapsed();
  timer.restart();
  block_widget.setFastIndex(fast_blocks);
  huffman_widget.setView(huffman);
  decode_widget.setView(decode);
  const qint64 hot_ms = timer.elapsed();
  qInfo() << "compression inspector set-model cold_ms=" << cold_ms
          << "hot_ms=" << hot_ms;
  QVERIFY(cold_ms < kSetModelMaxMs);
  QVERIFY(hot_ms < kSetModelMaxMs);

  auto* blocks_table = block_widget.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  auto* huffman_table = huffman_widget.findChild<QTableView*>(
      QStringLiteral("compressionHuffmanTable"));
  auto* decode_table = decode_widget.findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  assert_model_view(blocks_table);
  assert_model_view(huffman_table);
  assert_model_view(decode_table);
  // No QTableWidget may remain anywhere in the pages.
  QVERIFY(block_widget.findChildren<QTableWidget*>().isEmpty());
  QVERIFY(huffman_widget.findChildren<QTableWidget*>().isEmpty());
  QVERIFY(decode_widget.findChildren<QTableWidget*>().isEmpty());

  // Model contract: each table exposes the complete published source fact —
  // no truncation, no cap row.
  QCOMPARE(blocks_table->model()->rowCount(), kLargeBlockRows);
  QCOMPARE(huffman_table->model()->rowCount(), kMaxHuffmanTableEntries);
  QCOMPARE(decode_table->model()->rowCount(), kBoundedTraceTokens);

  // Typed projection facts behind deterministic rows: the formatted cells are
  // derived from these fields on demand.
  for (const int row : {0, kLargeBlockRows / 2, kLargeBlockRows - 1}) {
    const QVariant value = blocks_table->model()->data(
        blocks_table->model()->index(row, 0),
        pnga::ui::qt::BlockIndexRole);
    QCOMPARE(value.value<qulonglong>(), static_cast<qulonglong>(row));
  }
  for (const int row : {0, kMaxHuffmanTableEntries / 2,
                        kMaxHuffmanTableEntries - 1}) {
    const QVariant value = huffman_table->model()->data(
        huffman_table->model()->index(row, 0),
        pnga::ui::qt::HuffmanEntryRole);
    QCOMPARE(value.value<pnga::analysis_engine::HuffmanInspectorEntry>()
                 .symbol,
             static_cast<std::uint16_t>(row));
  }
  for (const int row :
       {0, kBoundedTraceTokens / 2, kBoundedTraceTokens - 1}) {
    const QVariant value = decode_table->model()->data(
        decode_table->model()->index(row, 0),
        pnga::ui::qt::DecodeTraceStepRole);
    QCOMPARE(
        value.value<pnga::analysis_engine::DecodeTraceStep>().token_index,
        static_cast<std::uint64_t>(row));
  }

  // No retained duplicate token/output buffers: the pages keep exactly the
  // published immutable projections (typed ranges only).
  QVERIFY(decode_widget.view() == decode);
  QVERIFY(huffman_widget.view() == huffman);

  // Give the views real viewport metrics so the scroll pass exercises the
  // on-demand formatting path the user sees.
  for (QTableView* view : {blocks_table, huffman_table, decode_table}) {
    view->resize(800, 600);
    view->show();
  }
  QCoreApplication::processEvents();

  // Deterministic 200-scroll response over all three tables.
  quint64 checksum = 0;
  qint64 blocks_ms = 0;
  qint64 huffman_ms = 0;
  qint64 decode_ms = 0;
  scroll_read_pass(blocks_table, kLargeBlockRows,
                   pnga::ui::qt::BlockInspectorModel::ColumnCount, 1,
                   &blocks_ms, &checksum);
  scroll_read_pass(huffman_table, kMaxHuffmanTableEntries,
                   pnga::ui::qt::HuffmanInspectorModel::ColumnCount, 0,
                   &huffman_ms, &checksum);
  scroll_read_pass(decode_table, kBoundedTraceTokens,
                   pnga::ui::qt::DecodeTraceModel::ColumnCount, 1,
                   &decode_ms, &checksum);
  qInfo() << "compression inspector scroll pass blocks_ms=" << blocks_ms
          << "huffman_ms=" << huffman_ms << "decode_ms=" << decode_ms
          << "checksum=" << checksum;
  QVERIFY(blocks_ms + huffman_ms + decode_ms < kScrollPassMaxMs);

  // Scrolling must not create per-row widgets.
  assert_model_view(blocks_table);
  assert_model_view(huffman_table);
  assert_model_view(decode_table);
}

void TraceInspectorPerformanceTest::hexHighlightsAreCapped() {
  auto source = std::make_shared<pnga::io::MemoryByteSource>(
      std::vector<std::byte>(10000, std::byte{0}));
  pnga::ui::qt::HexView view;
  view.setSource(pnga::ui::qt::make_file_hex_source(source));
  std::vector<pnga::ui::qt::HexHighlightSpan> spans;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    spans.push_back({i, 1, QColor(Qt::yellow)});
  }
  view.setHighlight(std::move(spans));
  QCOMPARE(view.highlightCount(),
           pnga::ui::qt::HexView::kMaxHighlightSpans);
}

QTEST_MAIN(TraceInspectorPerformanceTest)
#include "trace_inspector_performance_test.moc"
