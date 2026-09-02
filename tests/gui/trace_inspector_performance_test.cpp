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

#include <QElapsedTimer>
#include <QColor>
#include <QTableView>
#include <QTableWidget>

class TraceInspectorPerformanceTest : public QObject {
  Q_OBJECT
 private slots:
  void largeViewsAreCappedAndFast();
  void hexHighlightsAreCapped();
};

void TraceInspectorPerformanceTest::largeViewsAreCappedAndFast() {
  // WP-5U12C mechanism migration: the Blocks page is model-backed, so the
  // 10,000-row input is published through the Fast Index and the complete
  // block count is asserted on the model. The Huffman and Decode Trace
  // sections and every threshold are unchanged.
  pnga::analysis_engine::FastCompressionIndexView fast_blocks;
  fast_blocks.status =
      pnga::analysis_engine::FastCompressionIndexStatus::kReady;
  fast_blocks.generation = 1;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    pnga::analysis_engine::FastCompressionBlockRow row;
    row.block_index = i;
    row.output_range = {pnga::trace_model::InflatedByteOffset{i},
                        pnga::trace_model::InflatedByteOffset{i + 1}};
    fast_blocks.blocks.push_back(row);
  }
  pnga::analysis_engine::HuffmanInspectorView huffman;
  huffman.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  pnga::analysis_engine::HuffmanInspectorTable table;
  table.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  for (std::uint16_t i = 0; i < 5000; ++i) {
    table.entries.push_back({i, 1, 0, 0, 1, false});
  }
  huffman.tables.push_back(table);
  pnga::analysis_engine::DecodeTraceInspectorView decode;
  decode.status = pnga::analysis_engine::DecodeTraceInspectorStatus::kReady;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    pnga::analysis_engine::DecodeTraceStep step;
    step.token_index = i;
    step.output_end = i + 1;
    decode.steps.push_back(step);
  }

  pnga::ui::qt::BlockInspector block_widget;
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;
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
  qInfo() << "trace inspector render cold_ms=" << cold_ms
          << "hot_ms=" << hot_ms;
  QVERIFY(cold_ms < 2000);
  QVERIFY(hot_ms < 2000);
  // Model contract: the complete block list is exposed with a virtualized
  // row count equal to the source fact (the former capped QTableWidget row
  // count kMaxVisibleRows + 1 has no model equivalent).
  auto* blocks_table = block_widget.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(blocks_table != nullptr);
  QVERIFY(blocks_table->model() != nullptr);
  QCOMPARE(blocks_table->model()->rowCount(), 10000);
  QCOMPARE(huffman_widget.findChild<QTableWidget*>(
                QStringLiteral("huffmanInspectorTable"))
                ->rowCount(),
           pnga::ui::qt::HuffmanInspector::kMaxVisibleRows + 1);
  QCOMPARE(decode_widget.findChild<QTableWidget*>(
                QStringLiteral("decodeTraceInspectorTable"))
                ->rowCount(),
           pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows + 1);
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
