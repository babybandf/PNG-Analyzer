// WP-5U12/M5: binding tests. The binding publishes one generation-coherent
// state to all three pages, feeds the shared WP-5U12B store so the Blocks
// page can render Current/Manual Selection, and formats the stream summary
// from the Fast Index contract fields only.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/trace_query.h>
#include <pnga/analysis-engine/trace_inspector_state.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/trace_inspector_binding.h>

#include <QtTest/QtTest>

#include <QLabel>
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

pnga::analysis_engine::FastCompressionIndexView readyIndex(
    std::uint64_t generation) {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 3), idat(200, 4)};
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

pnga::analysis_engine::TraceQueryResult readyTrace() {
  pnga::analysis_engine::TraceQueryResult result;
  result.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  result.generation = 91;
  pnga::analysis_engine::TraceBlockSummary block;
  block.index = 0;
  block.type = pnga::deflate_index::BlockType::kFixed;
  block.output_begin = 0;
  block.output_end = 10;
  result.blocks.push_back(block);
  return result;
}

QTableView* blocksTable(pnga::ui::qt::BlockInspector& widget) {
  return widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
}

}  // namespace

class TraceInspectorBindingTest : public QObject {
  Q_OBJECT
 private slots:
  void publishesOneGenerationToAllPages();
  void publishesLifecycleStatus();
  void fastIndexIsVisibleWithoutPixelLock();
  void storeStateReachesBlocksPage();
  void publishMapsCurrentThroughStore();
};

void TraceInspectorBindingTest::publishesOneGenerationToAllPages() {
  pnga::analysis_engine::TraceQueryResult result = readyTrace();

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
  pnga::ui::qt::CompressionContext context;
  pnga::ui::qt::TraceInspectorBinding binding(&block, &huffman, &decode);
  binding.setContext(&context);
  pnga::analysis_engine::TraceInspectorState state;
  state.generation = 12;
  state.status = pnga::analysis_engine::TraceInspectorLifecycle::kReplaying;
  binding.publishState(state);
  QVERIFY(context.findChild<QLabel*>(QStringLiteral("compressionContextStatus"))
              ->text()
              .contains(QStringLiteral("Replaying")));
}

void TraceInspectorBindingTest::fastIndexIsVisibleWithoutPixelLock() {
  pnga::ui::qt::BlockInspector block_widget;
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  pnga::ui::qt::CompressionContext context;
  pnga::ui::qt::TraceInspectorBinding binding(
      &block_widget, &huffman_widget, &decode_widget);
  binding.setContext(&context);

  // The complete Fast Index is published before any trace request exists:
  // no pixel lock and no Deep Trace are required to browse the Blocks list.
  binding.publishFastIndex(readyIndex(3));
  QVERIFY(blocksTable(block_widget) != nullptr);
  QCOMPARE(blocksTable(block_widget)->model()->rowCount(), 3);
  QVERIFY(!block_widget.view().rows.empty() ||
          blocksTable(block_widget)->model()->rowCount() > 0);

  // The shared summary is formatted from the contract fields: the IDAT
  // segment count comes from idat_spans.size() and Adler from adler.status.
  auto* summary = context.findChild<QLabel*>(
      QStringLiteral("compressionContextStreamSummary"));
  QVERIFY(summary != nullptr);
  QVERIFY(summary->text().contains(QStringLiteral("2 IDAT segments")));
  QVERIFY(summary->text().contains(QStringLiteral("Adler valid")));
  // No trace bundle exists yet; the context must not claim a trace.
  auto* status = context.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QVERIFY(!status->text().contains(QStringLiteral("Trace ready")));
}

void TraceInspectorBindingTest::storeStateReachesBlocksPage() {
  pnga::ui::qt::BlockInspector block_widget;
  // The store is discovered through the page's window exactly like in the
  // product window, where MainWindow owns it.
  auto* store = new pnga::ui::qt::CompressionSelectionStore(&block_widget);
  store->resetGeneration(5);
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  pnga::ui::qt::TraceInspectorBinding binding(
      &block_widget, &huffman_widget, &decode_widget);
  binding.publishFastIndex(readyIndex(5));

  pnga::trace_model::CompressionNavigationTarget manual;
  manual.generation = 5;
  manual.request_serial = 1;
  manual.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  manual.logical_range = pnga::trace_model::ZlibBitRange{
      pnga::trace_model::ZlibBitOffset{40},
      pnga::trace_model::ZlibBitOffset{56}};
  manual.physical_spans = {pnga::trace_model::FileByteRange{
      pnga::trace_model::FileByteOffset{200},
      pnga::trace_model::FileByteOffset{204}}};
  manual.block_index = 2;
  QVERIFY(store->applyNavigation(manual));

  auto* model = blocksTable(block_widget)->model();
  QVERIFY(model != nullptr);
  QVERIFY(model->data(model->index(2, 0),
                      pnga::ui::qt::IsManualSelectionRole)
              .toBool());
  QVERIFY(!model->data(model->index(0, 0),
                       pnga::ui::qt::IsManualSelectionRole)
               .toBool());
}

void TraceInspectorBindingTest::publishMapsCurrentThroughStore() {
  pnga::ui::qt::BlockInspector block_widget;
  auto* store = new pnga::ui::qt::CompressionSelectionStore(&block_widget);
  store->resetGeneration(91);
  pnga::ui::qt::HuffmanInspector huffman_widget;
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  pnga::ui::qt::TraceInspectorBinding binding(
      &block_widget, &huffman_widget, &decode_widget);
  binding.publishFastIndex(readyIndex(91));

  // A committed pixel publishes the Current mapping through the shared
  // store; the immutable Current and a Manual Selection coexist.
  binding.publish(readyTrace(), std::nullopt, 4, 0);
  QVERIFY(store->state().current.has_value());
  QCOMPARE(store->state().current->block_index,
           std::optional<std::uint64_t>{0});
  QCOMPARE(store->state().current->output_range,
           (pnga::trace_model::InflatedByteRange{
               pnga::trace_model::InflatedByteOffset{4},
               pnga::trace_model::InflatedByteOffset{5}}));

  // Manual selection survives the Current update (store semantics) and both
  // roles light on the Blocks model simultaneously.
  pnga::trace_model::CompressionNavigationTarget manual;
  manual.generation = 91;
  manual.request_serial = 2;
  manual.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  manual.logical_range = pnga::trace_model::ZlibBitRange{
      pnga::trace_model::ZlibBitOffset{24},
      pnga::trace_model::ZlibBitOffset{40}};
  manual.physical_spans = {pnga::trace_model::FileByteRange{
      pnga::trace_model::FileByteOffset{200},
      pnga::trace_model::FileByteOffset{202}}};
  manual.block_index = 1;
  QVERIFY(store->setManual(manual));
  auto* model = blocksTable(block_widget)->model();
  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::ContainsCurrentRole)
              .toBool());
  QVERIFY(model->data(model->index(1, 0),
                      pnga::ui::qt::IsManualSelectionRole)
              .toBool());
}

QTEST_MAIN(TraceInspectorBindingTest)
#include "trace_inspector_binding_test.moc"
