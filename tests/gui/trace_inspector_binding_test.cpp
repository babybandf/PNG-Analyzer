#include <pnga/analysis-engine/trace_query.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/trace_inspector_binding.h>

#include <QtTest/QtTest>

class TraceInspectorBindingTest : public QObject {
  Q_OBJECT
 private slots:
  void publishesOneGenerationToAllPages();
};

void TraceInspectorBindingTest::publishesOneGenerationToAllPages() {
  pnga::analysis_engine::TraceQueryResult result;
  result.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  result.generation = 91;
  pnga::analysis_engine::TraceBlockSummary block;
  block.index = 0;
  block.type = pnga::deflate_index::BlockType::kFixed;
  block.output_begin = 0;
  block.output_end = 1;
  result.blocks.push_back(block);

  pnga::ui::qt::BlockInspector block_widget;
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  pnga::ui::qt::TraceInspectorBinding binding(
      &block_widget, &huffman_widget, &decode_widget);
  QSignalSpy spy(&binding,
                 &pnga::ui::qt::TraceInspectorBinding::generationPublished);
  binding.publish(result, std::nullopt, 0, 3);
  QCOMPARE(binding.generation(), std::uint64_t{91});
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toULongLong(), qulonglong{91});
  QCOMPARE(block_widget.view().generation, std::uint64_t{91});
  QCOMPARE(huffman_widget.view().generation, std::uint64_t{91});
  QCOMPARE(decode_widget.view().generation, std::uint64_t{91});
}

QTEST_MAIN(TraceInspectorBindingTest)
#include "trace_inspector_binding_test.moc"
