// WP-5U12C: model/view Blocks page tests. The Blocks table must be a
// QTableView driven by BlockInspectorModel (no QTableWidget, no per-row
// widgets), expose the exact normative columns, keep Current and Manual
// Selection simultaneously visible, retain verified rows on Partial/Error,
// support native keyboard navigation and emit only typed actions.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/block_inspector_model.h>
#include <pnga/ui/qt/compression_selection_store.h>

#include <QtTest/QtTest>

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>

#include <cstdint>
#include <memory>
#include <vector>

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
    std::uint64_t generation) {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 3), idat(200, 4)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.success = true;
  index.zlib_header_bits = 16;
  index.total_output_bytes = 12;
  index.wrapper = pnga::deflate_index::ZlibWrapperInfo{0x78, 0x9C, 8, 15,
                                                       false, true};
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

pnga::analysis_engine::FastCompressionIndexView partialIndex(
    std::uint64_t generation) {
  pnga::png_format::ChunkIndex chunks;
  chunks.chunks = {idat(100, 2)};
  const pnga::png_format::VirtualIDATStream stream(chunks);
  pnga::deflate_index::BlockIndexResult index;
  index.error = "truncated zlib stream (no end marker)";
  index.zlib_header_bits = 16;
  index.blocks.push_back({0, pnga::deflate_index::BlockType::kStored, false,
                          8, 16, 0, 1});
  index.stop_input_bit = 16;
  index.stop_output_byte = 1;
  return pnga::analysis_engine::build_fast_compression_index(
      generation, index, stream);
}

pnga::trace_model::CompressionNavigationTarget blockTarget(
    std::uint64_t generation, std::uint64_t serial,
    std::uint64_t block_index) {
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = generation;
  target.request_serial = serial;
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  target.logical_range = pnga::trace_model::ZlibBitRange{
      pnga::trace_model::ZlibBitOffset{8},
      pnga::trace_model::ZlibBitOffset{24}};
  target.physical_spans = {
      pnga::trace_model::FileByteRange{
          pnga::trace_model::FileByteOffset{100},
          pnga::trace_model::FileByteOffset{103}}};
  target.block_index = block_index;
  return target;
}

}  // namespace

class BlockInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersModelBackedTableWithNormativeColumns();
  void keepsCurrentAndSelectionSimultaneously();
  void retainsVerifiedRowsOnPartialIndex();
  void keyboardNavigatesRows();
  void rowSelectionProducesManualTargetWithoutNavigation();
  void actionsEmitTypedTargets();
  void detailsShowSpansStoredMetadataAndStopFacts();
  void sameGenerationRepublishPreservesManualWidths();
  void generationChangeRefitsColumnsAndMarker();
};

void BlockInspectorTest::rendersModelBackedTableWithNormativeColumns() {
  pnga::ui::qt::BlockInspector widget;
  const auto index =
      std::make_shared<const pnga::analysis_engine::FastCompressionIndexView>(
          readyIndex(7));
  widget.setFastIndex(*index);

  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  // The Blocks table is model-backed: no QTableWidget may remain anywhere in
  // the page and no row may own an index widget.
  QVERIFY(widget.findChildren<QTableWidget*>().isEmpty());
  auto* model = view->model();
  QVERIFY(model != nullptr);
  QCOMPARE(model->rowCount(), 3);  // complete list, no truncation row
  QCOMPARE(model->columnCount(), 8);
  const QStringList expected_headers{
      QStringLiteral("Current"), QStringLiteral("#"),
      QStringLiteral("Type"),    QStringLiteral("Final"),
      QStringLiteral("Input bits"), QStringLiteral("Output bytes"),
      QStringLiteral("Events"),  QStringLiteral("Scanlines")};
  QStringList headers;
  for (int column = 0; column < model->columnCount(); ++column) {
    headers << model->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                   .toString();
  }
  QCOMPARE(headers, expected_headers);
  // User column resizing contract (defect 2026-09-05): every content column
  // stays user-adjustable (QHeaderView::Interactive); only the Current
  // marker keeps its fixed 28 px section and the two fill columns keep the
  // normative Stretch mode whose viewport-derived widths are locked into the
  // WP-5U12F baselines.
  auto* header = view->horizontalHeader();
  QCOMPARE(header->sectionResizeMode(pnga::ui::qt::BlockInspectorModel::Current),
           QHeaderView::Fixed);
  for (const int column :
       {pnga::ui::qt::BlockInspectorModel::Number,
        pnga::ui::qt::BlockInspectorModel::Type,
        pnga::ui::qt::BlockInspectorModel::Final,
        pnga::ui::qt::BlockInspectorModel::Events,
        pnga::ui::qt::BlockInspectorModel::Scanlines}) {
    QCOMPARE(header->sectionResizeMode(column), QHeaderView::Interactive);
  }
  QCOMPARE(header->sectionResizeMode(
               pnga::ui::qt::BlockInspectorModel::InputBits),
           QHeaderView::Stretch);
  QCOMPARE(header->sectionResizeMode(
               pnga::ui::qt::BlockInspectorModel::OutputBytes),
           QHeaderView::Stretch);
  for (int row = 0; row < model->rowCount(); ++row) {
    for (int column = 0; column < model->columnCount(); ++column) {
      const QModelIndex cell = model->index(row, column);
      QVERIFY(view->indexWidget(cell) == nullptr);
    }
  }
  // Row 0 cell values.
  QCOMPARE(model->data(model->index(0, 1), Qt::DisplayRole).toString(),
           QStringLiteral("0"));
  QCOMPARE(model->data(model->index(0, 2), Qt::DisplayRole).toString(),
           QStringLiteral("stored"));
  QCOMPARE(model->data(model->index(0, 3), Qt::DisplayRole).toString(),
           QStringLiteral("no"));
  QCOMPARE(model->data(model->index(0, 4), Qt::DisplayRole).toString(),
           QStringLiteral("8–24"));
  QCOMPARE(model->data(model->index(0, 5), Qt::DisplayRole).toString(),
           QStringLiteral("0–3"));
  QCOMPARE(model->data(model->index(0, 6), Qt::DisplayRole).toString(),
           QStringLiteral("—"));
  QCOMPARE(model->data(model->index(0, 7), Qt::DisplayRole).toString(),
           QStringLiteral("—"));
  // Typed roles carry the immutable facts.
  QCOMPARE(model->data(model->index(0, 0),
                       pnga::ui::qt::BlockIndexRole)
               .toULongLong(),
           qulonglong{0});
  QCOMPARE((model->data(model->index(0, 0),
                         pnga::ui::qt::InputRangeRole)
                .value<pnga::trace_model::ZlibBitRange>()),
           (pnga::trace_model::ZlibBitRange{
               pnga::trace_model::ZlibBitOffset{8},
               pnga::trace_model::ZlibBitOffset{24}}));
  QCOMPARE((model->data(model->index(0, 0),
                         pnga::ui::qt::OutputRangeRole)
                .value<pnga::trace_model::InflatedByteRange>()),
           (pnga::trace_model::InflatedByteRange{
               pnga::trace_model::InflatedByteOffset{0},
               pnga::trace_model::InflatedByteOffset{3}}));
  QCOMPARE(model->data(model->index(0, 0),
                       pnga::ui::qt::PhysicalSpansRole)
               .value<std::vector<pnga::trace_model::ProvenanceSpan>>()
               .size(),
           std::size_t{1});
  // Accessible text follows the normative example.
  QCOMPARE(model->data(model->index(2, 0),
                       pnga::ui::qt::AccessibleTextRole)
               .toString(),
           QStringLiteral("DEFLATE block 2, dynamic, final"));
  // Unproven event/scanline facts stay explicit in accessible text too.
  QVERIFY(!model->data(model->index(0, 0),
                       pnga::ui::qt::AccessibleTextRole)
               .toString()
               .isEmpty());
}

void BlockInspectorTest::keepsCurrentAndSelectionSimultaneously() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  auto* model = view->model();
  QVERIFY(model != nullptr);

  pnga::trace_model::CompressionSelectionState state;
  state.generation = 7;
  pnga::trace_model::CompressionCurrentMapping current;
  current.generation = 7;
  current.source_unit = pnga::trace_model::DocumentSourceUnit{};
  current.output_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{1},
      pnga::trace_model::InflatedByteOffset{2}};
  current.block_index = 0;
  state.current = current;
  state.manual = blockTarget(7, 1, 2);
  dynamic_cast<pnga::ui::qt::BlockInspectorModel*>(model)
      ->setSelectionState(state);

  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::ContainsCurrentRole)
              .toBool());
  QVERIFY(!model->data(model->index(1, 0),
                       pnga::ui::qt::ContainsCurrentRole)
               .toBool());
  QVERIFY(model->data(model->index(2, 0),
                      pnga::ui::qt::IsManualSelectionRole)
              .toBool());
  QVERIFY(!model->data(model->index(0, 0),
                       pnga::ui::qt::IsManualSelectionRole)
               .toBool());
  // Current glyph and accessible current text coexist with the manual row.
  QCOMPARE(model->data(model->index(0, 0), Qt::DisplayRole).toString(),
           QStringLiteral("●"));
  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::AccessibleTextRole)
              .toString()
              .contains(QStringLiteral("contains current output")));
  // A stale generation must not light any row.
  pnga::trace_model::CompressionSelectionState stale = state;
  stale.generation = 6;
  dynamic_cast<pnga::ui::qt::BlockInspectorModel*>(model)
      ->setSelectionState(stale);
  QVERIFY(!model->data(model->index(0, 0),
                       pnga::ui::qt::ContainsCurrentRole)
               .toBool());
  QVERIFY(!model->data(model->index(2, 0),
                       pnga::ui::qt::IsManualSelectionRole)
               .toBool());
}

void BlockInspectorTest::retainsVerifiedRowsOnPartialIndex() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(partialIndex(9));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  auto* model = view->model();
  QVERIFY(model != nullptr);
  // Verified rows stay browsable; nothing is replaced by an error panel.
  QCOMPARE(model->rowCount(), 1);
  QCOMPARE(model->data(model->index(0, 1), Qt::DisplayRole).toString(),
           QStringLiteral("0"));
  auto* details_title = widget.findChild<QLabel*>(
      QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  // Selecting the retained row keeps its details available.
  view->selectRow(0);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #0")));
}

void BlockInspectorTest::keyboardNavigatesRows() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(readyIndex(7));
  widget.resize(600, 400);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  view->selectRow(0);
  QCOMPARE(view->selectionModel()->currentIndex().row(), 0);
  QTest::keyClick(view, Qt::Key_Down);
  QCOMPARE(view->selectionModel()->currentIndex().row(), 1);
  QTest::keyClick(view, Qt::Key_Up);
  QCOMPARE(view->selectionModel()->currentIndex().row(), 0);
  QTest::keyClick(view, Qt::Key_End);
  QCOMPARE(view->selectionModel()->currentIndex().row(), 2);
  QTest::keyClick(view, Qt::Key_Home);
  QCOMPARE(view->selectionModel()->currentIndex().row(), 0);
}

void BlockInspectorTest::rowSelectionProducesManualTargetWithoutNavigation() {
  pnga::ui::qt::CompressionSelectionStore store;
  store.resetGeneration(7);
  pnga::ui::qt::BlockInspector widget;
  widget.setSelectionStore(&store);
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);

  QSignalSpy navigation_spy(
      &widget, &pnga::ui::qt::BlockInspector::navigationRequested);
  QVERIFY(navigation_spy.isValid());
  view->selectRow(1);
  // Row selection changes only Manual Selection: no navigation request, no
  // history entry, no trace submission of any kind.
  QCOMPARE(navigation_spy.count(), 0);
  QVERIFY(store.state().manual.has_value());
  QCOMPARE(store.state().manual->block_index,
           std::optional<std::uint64_t>{1});
  QCOMPARE(store.state().manual->origin,
           pnga::trace_model::CompressionNavigationOrigin::kBlocks);
  QCOMPARE(store.history().size(), std::size_t{0});
  // Details follow the manual selection.
  auto* details_title = widget.findChild<QLabel*>(
      QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #1")));

  // Without a store the typed emission stays available.
  pnga::ui::qt::BlockInspector standalone;
  standalone.setFastIndex(readyIndex(7));
  QSignalSpy standalone_spy(
      &standalone, &pnga::ui::qt::BlockInspector::navigationRequested);
  QVERIFY(standalone_spy.isValid());
  auto* standalone_view = standalone.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(standalone_view != nullptr);
  standalone_view->selectRow(0);
  QCOMPARE(standalone_spy.count(), 1);
  const auto target =
      standalone_spy.takeFirst()
          .at(0)
          .value<pnga::trace_model::CompressionNavigationTarget>();
  QVERIFY(target.valid());
  QCOMPARE(target.block_index, std::optional<std::uint64_t>{0});
}

void BlockInspectorTest::actionsEmitTypedTargets() {
  pnga::ui::qt::CompressionSelectionStore store;
  store.resetGeneration(7);
  pnga::ui::qt::BlockInspector widget;
  widget.setSelectionStore(&store);
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  view->selectRow(0);

  bool navigated = false;
  pnga::trace_model::CompressionNavigationTarget hex_target;
  QObject::connect(&store,
                   &pnga::ui::qt::CompressionSelectionStore::navigationRequested,
                   &widget,
                   [&](const pnga::trace_model::CompressionNavigationTarget&
                           target) {
                     navigated = true;
                     hex_target = target;
                   });
  const auto buttons = widget.findChildren<QPushButton*>();
  QPushButton* hex_button = nullptr;
  QPushButton* inflated_button = nullptr;
  QPushButton* trace_button = nullptr;
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      hex_button = button;
    } else if (button->text() ==
               QStringLiteral("Show inflated output")) {
      inflated_button = button;
    } else if (button->text() == QStringLiteral("Open Decode Trace")) {
      trace_button = button;
    }
  }
  QVERIFY(hex_button != nullptr);
  QVERIFY(inflated_button != nullptr);
  QVERIFY(trace_button != nullptr);

  // Show in Hex asks B navigation with the row's ZlibBitRange and every
  // physical span in order.
  hex_button->click();
  QVERIFY(navigated);
  QVERIFY(hex_target.valid());
  QVERIFY(std::holds_alternative<pnga::trace_model::ZlibBitRange>(
      hex_target.logical_range));
  QCOMPARE(hex_target.block_index, std::optional<std::uint64_t>{0});
  QCOMPARE(hex_target.physical_spans.size(), std::size_t{1});
  QCOMPARE(hex_target.physical_spans.front(),
           (pnga::trace_model::FileByteRange{
               pnga::trace_model::FileByteOffset{101},
               pnga::trace_model::FileByteOffset{103}}));
  QCOMPARE(store.history().size(), std::size_t{1});

  // Show inflated output carries only the InflatedByteRange.
  inflated_button->click();
  QCOMPARE(store.history().size(), std::size_t{2});
  const auto& inflated = store.history().back();
  QVERIFY(std::holds_alternative<pnga::trace_model::InflatedByteRange>(
      inflated.logical_range));
  QVERIFY(inflated.physical_spans.empty());
  QVERIFY(inflated.valid());

  // Open Decode Trace emits the typed bounded-trace request exactly once.
  QSignalSpy trace_spy(
      &widget, &pnga::ui::qt::BlockInspector::decodeTraceRequested);
  QVERIFY(trace_spy.isValid());
  trace_button->click();
  QCOMPARE(trace_spy.count(), 1);
  QCOMPARE(trace_spy.at(0).at(0).toULongLong(), qulonglong{7});
  QCOMPARE(trace_spy.at(0).at(1).toULongLong(), qulonglong{0});
  const auto output_range = trace_spy.at(0).at(2)
                                .value<pnga::trace_model::InflatedByteRange>();
  QCOMPARE(output_range,
           (pnga::trace_model::InflatedByteRange{
               pnga::trace_model::InflatedByteOffset{0},
               pnga::trace_model::InflatedByteOffset{3}}));
}

void BlockInspectorTest::detailsShowSpansStoredMetadataAndStopFacts() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  view->selectRow(0);
  const auto labels = widget.findChild<QWidget*>(
                          QStringLiteral("compressionDetailsBody"))
                          ->findChildren<QLabel*>();
  bool found_spans = false;
  bool found_stored = false;
  bool found_unproven_events = false;
  for (const auto* label : labels) {
    if (label->text().contains(QStringLiteral("file[101..103)"))) {
      found_spans = true;
    }
    if (label->text() == QStringLiteral("3 bytes")) {
      found_stored = true;
    }
    if (label->text() == QStringLiteral("—")) {
      found_unproven_events = true;
    }
  }
  QVERIFY(found_spans);
  QVERIFY(found_stored);
  QVERIFY(found_unproven_events);

  // A partial index surfaces the verified stop location in the details.
  pnga::ui::qt::BlockInspector partial_widget;
  partial_widget.setFastIndex(partialIndex(9));
  auto* partial_view = partial_widget.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(partial_view != nullptr);
  partial_view->selectRow(0);
  const auto partial_labels =
      partial_widget.findChild<QWidget*>(
          QStringLiteral("compressionDetailsBody"))
          ->findChildren<QLabel*>();
  bool found_stop = false;
  bool found_error = false;
  for (const auto* label : partial_labels) {
    if (label->text().contains(QStringLiteral("zlib bit 16")) &&
        label->text().contains(QStringLiteral("output byte 1"))) {
      found_stop = true;
    }
    if (label->text().contains(QStringLiteral("truncated zlib stream"))) {
      found_error = true;
    }
  }
  QVERIFY(found_stop);
  QVERIFY(found_error);
}

void BlockInspectorTest::sameGenerationRepublishPreservesManualWidths() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  // A manual width adjustment is a user decision: a same-generation
  // republish (row publish, Current change, selection change) must not
  // reset it.
  view->setColumnWidth(pnga::ui::qt::BlockInspectorModel::Type, 200);
  widget.setFastIndex(readyIndex(7));
  QCOMPARE(view->columnWidth(pnga::ui::qt::BlockInspectorModel::Type), 200);
  QCOMPARE(view->columnWidth(pnga::ui::qt::BlockInspectorModel::Current), 28);
}

void BlockInspectorTest::generationChangeRefitsColumnsAndMarker() {
  pnga::ui::qt::BlockInspector widget;
  widget.setFastIndex(readyIndex(7));
  auto* view =
      widget.findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(view != nullptr);
  view->setColumnWidth(pnga::ui::qt::BlockInspectorModel::Type, 200);
  // A document open publishes a new generation: the widths re-derive from
  // content into the normative fresh-open geometry and the marker keeps its
  // fixed 28 px section.
  widget.setFastIndex(readyIndex(8));
  pnga::ui::qt::BlockInspector reference;
  reference.setFastIndex(readyIndex(8));
  auto* reference_view = reference.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(reference_view != nullptr);
  QCOMPARE(view->columnWidth(pnga::ui::qt::BlockInspectorModel::Type),
           reference_view->columnWidth(
               pnga::ui::qt::BlockInspectorModel::Type));
  QCOMPARE(view->columnWidth(pnga::ui::qt::BlockInspectorModel::Current), 28);
}

QTEST_MAIN(BlockInspectorTest)
#include "block_inspector_test.moc"
