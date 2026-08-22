#include <pnga/analysis-engine/trace_query.h>
#include <pnga/analysis-engine/trace_inspector_state.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/trace_inspector_binding.h>

#include <QtTest/QtTest>

#include <QLabel>

class TraceInspectorBindingTest : public QObject {
  Q_OBJECT
 private slots:
  void publishesOneGenerationToAllPages();
  void publishesLifecycleStatus();
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

void TraceInspectorBindingTest::publishesLifecycleStatus() {
  pnga::ui::qt::BlockInspector block;
  pnga::ui::qt::HuffmanInspector huffman;
  pnga::ui::qt::DecodeTraceInspector decode;
  pnga::ui::qt::TraceInspectorBinding binding(&block, &huffman, &decode);
  pnga::analysis_engine::TraceInspectorState state;
  state.generation = 12;
  state.status = pnga::analysis_engine::TraceInspectorLifecycle::kReplaying;
  binding.publishState(state);
  QVERIFY(block.findChild<QLabel*>(QStringLiteral("blockInspectorStatus"))
              ->text()
              .contains(QStringLiteral("replaying")));
}

QTEST_MAIN(TraceInspectorBindingTest)
#include "trace_inspector_binding_test.moc"
