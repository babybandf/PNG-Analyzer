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
#include <QTableWidget>

class TraceInspectorPerformanceTest : public QObject {
  Q_OBJECT
 private slots:
  void largeViewsAreCappedAndFast();
  void hexHighlightsAreCapped();
};

void TraceInspectorPerformanceTest::largeViewsAreCappedAndFast() {
  pnga::analysis_engine::BlockInspectorView block;
  block.status = pnga::analysis_engine::BlockInspectorStatus::kReady;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    pnga::analysis_engine::BlockInspectorRow row;
    row.block_index = i;
    row.output_end = i + 1;
    block.rows.push_back(row);
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
  block_widget.setView(block);
  huffman_widget.setView(huffman);
  decode_widget.setView(decode);
  const qint64 cold_ms = timer.elapsed();
  timer.restart();
  block_widget.setView(block);
  huffman_widget.setView(huffman);
  decode_widget.setView(decode);
  const qint64 hot_ms = timer.elapsed();
  qInfo() << "trace inspector render cold_ms=" << cold_ms
          << "hot_ms=" << hot_ms;
  QVERIFY(cold_ms < 2000);
  QVERIFY(hot_ms < 2000);
  QCOMPARE(block_widget.findChild<QTableWidget*>(
                QStringLiteral("blockInspectorTable"))
                ->rowCount(),
           pnga::ui::qt::BlockInspector::kMaxVisibleRows + 1);
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
