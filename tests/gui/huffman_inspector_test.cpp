// WP-5U12D Huffman page tests: the model-backed table renders immutable
// projection facts without bit reversal, hides zero-bit entries by default,
// coexists Current and Manual Selection, publishes typed B navigation for
// row selection and bounded occurrence openings, and keeps the Stored,
// Partial, keyboard, copy and accessibility contracts.

#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/huffman_inspector_model.h>

#include <QtTest/QtTest>

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

using pnga::analysis_engine::HuffmanBlockScope;
using pnga::analysis_engine::HuffmanInspectorEntry;
using pnga::analysis_engine::HuffmanInspectorStatus;
using pnga::analysis_engine::HuffmanInspectorTable;
using pnga::analysis_engine::HuffmanInspectorView;
using pnga::analysis_engine::HuffmanOccurrenceFact;
using pnga::analysis_engine::HuffmanTableMode;
using pnga::deflate_trace::HuffmanTableKind;
using pnga::trace_model::CompressionNavigationOrigin;
using pnga::trace_model::CompressionSelectionState;
using pnga::trace_model::DeflateBitOffset;
using pnga::trace_model::DeflateBitRange;
using pnga::trace_model::ProvenanceSpace;
using pnga::trace_model::ProvenanceSpan;

QTableView* huffmanTable(pnga::ui::qt::HuffmanInspector& widget) {
  return widget.findChild<QTableView*>(
      QStringLiteral("compressionHuffmanTable"));
}

HuffmanInspectorEntry entry(std::uint16_t symbol, std::uint8_t bit_length,
                            std::uint16_t canonical, std::string meaning,
                            std::string canonical_bits,
                            std::string read_order_bits,
                            std::vector<std::uint64_t> occurrences = {},
                            bool selected = false,
                            std::uint64_t provenance_begin = 0,
                            std::uint64_t provenance_end = 0) {
  HuffmanInspectorEntry result;
  result.symbol = symbol;
  result.bit_length = bit_length;
  result.canonical_code = canonical;
  result.provenance_range = DeflateBitRange{DeflateBitOffset{provenance_begin},
                                            DeflateBitOffset{provenance_end}};
  result.read_order_code = 0;
  for (std::uint8_t i = 0; i < bit_length; ++i) {
    result.read_order_code = static_cast<std::uint16_t>(
        (result.read_order_code << 1) | ((canonical >> i) & 1u));
  }
  result.meaning = std::move(meaning);
  result.canonical_bits = std::move(canonical_bits);
  result.read_order_bits = std::move(read_order_bits);
  result.occurrence_token_indices = std::move(occurrences);
  result.selected = selected;
  return result;
}

// Dynamic block #7: literal/length entries (including a hidden zero-bit
// entry), a distance table and a code-length table, plus bounded occurrence
// facts and the owning block's typed scope.
HuffmanInspectorView dynamic_view() {
  HuffmanInspectorView view;
  view.status = HuffmanInspectorStatus::kReady;
  view.generation = 3;

  HuffmanInspectorTable literal;
  literal.block_index = 7;
  literal.mode = HuffmanTableMode::kDynamic;
  literal.kind = HuffmanTableKind::kLiteralLength;
  literal.selector_label = "Literal / Length";
  literal.build_order = 1;
  literal.declared_entry_count = 4;
  literal.bounded_token_count = 4;
  literal.entries.push_back(entry(65, 3, 4, "literal 65", "100", "001",
                                  {3, 5}, false, 20, 23));
  literal.entries.push_back(
      entry(268, 3, 5, "length 17-18", "101", "101", {4}));
  literal.entries.push_back(entry(256, 1, 0, "end-of-block", "0", "0", {6}));
  literal.entries.push_back(entry(66, 0, 0, "literal 66", "", ""));
  view.tables.push_back(literal);

  HuffmanInspectorTable distance;
  distance.block_index = 7;
  distance.mode = HuffmanTableMode::kDynamic;
  distance.kind = HuffmanTableKind::kDistance;
  distance.selector_label = "Distance";
  distance.build_order = 2;
  distance.declared_entry_count = 1;
  distance.bounded_token_count = 4;
  distance.entries.push_back(entry(0, 1, 0, "distance 1", "0", "0"));
  view.tables.push_back(distance);

  HuffmanInspectorTable code_length;
  code_length.block_index = 7;
  code_length.mode = HuffmanTableMode::kDynamic;
  code_length.kind = HuffmanTableKind::kCodeLength;
  code_length.selector_label = "Code Length";
  code_length.build_order = 0;
  code_length.declared_entry_count = 2;
  code_length.bounded_token_count = 4;
  code_length.entries.push_back(
      entry(16, 2, 0, "repeat previous length 3-6", "00", "00"));
  code_length.entries.push_back(
      entry(17, 2, 1, "repeat zero length 3-10", "01", "10"));
  view.tables.push_back(code_length);

  view.occurrences.push_back(
      HuffmanOccurrenceFact{3, DeflateBitRange{DeflateBitOffset{4},
                                               DeflateBitOffset{12}}});
  view.occurrences.push_back(
      HuffmanOccurrenceFact{4, DeflateBitRange{DeflateBitOffset{12},
                                               DeflateBitOffset{20}}});
  view.occurrences.push_back(
      HuffmanOccurrenceFact{5, DeflateBitRange{DeflateBitOffset{20},
                                               DeflateBitOffset{28}}});

  HuffmanBlockScope scope;
  scope.block_index = 7;
  scope.deflate_range =
      DeflateBitRange{DeflateBitOffset{0}, DeflateBitOffset{64}};
  scope.physical_spans.push_back(
      ProvenanceSpan{ProvenanceSpace::kPhysicalFile, 100, 10, 0, 80, true});
  view.block_scopes.push_back(scope);
  return view;
}

CompressionSelectionState state_for(std::uint64_t generation) {
  CompressionSelectionState state;
  state.generation = generation;
  return state;
}

}  // namespace

class HuffmanInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void modelRendersProjectionWithExactHeaders();
  void zeroBitEntriesHiddenByDefaultAndAvailableOnDemand();
  void selectorOrderLabelsAndFiltering();
  void detailsContainBothBitOrdersAndProvenance();
  void storedBlockShowsExplicitNoHuffmanState();
  void partialResultRetainsRows();
  void currentAndManualSelectionCoexist();
  void rowSelectionPublishesManualTarget();
  void openOccurrenceNavigatesBoundedTokens();
  void openOccurrenceWithoutOccurrencesShowsExactText();
  void keyboardNavigationMovesRowSelection();
  void sameGenerationRepublishPreservesManualWidths();
  void generationChangeAndKindSwitchRefitColumns();
};

void HuffmanInspectorTest::initTestCase() {
  // The Current-entry background uses the centralized theme token; the
  // theme is installed (without persisting settings) so the color tokens
  // resolve on the offscreen platform as well.
  auto* theme = new pnga::ui::qt::ApplicationTheme(qApp, this);
  theme->setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                 /*persist=*/false);
}

void HuffmanInspectorTest::modelRendersProjectionWithExactHeaders() {
  const auto view = dynamic_view();
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  // Model/view contract: a QTableView over a QAbstractTableModel and no
  // QTableWidget or per-row index widget anywhere on the page.
  QVERIFY(widget.findChild<QTableWidget*>() == nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  auto* huffman_model =
      qobject_cast<pnga::ui::qt::HuffmanInspectorModel*>(model);
  QVERIFY(huffman_model != nullptr);

  QCOMPARE(model->rowCount(), 3);  // zero-bit entry hidden by default
  const QStringList headers = {QStringLiteral("Symbol"),
                               QStringLiteral("Meaning"),
                               QStringLiteral("Bits"),
                               QStringLiteral("Canonical"),
                               QStringLiteral("Read order"),
                               QStringLiteral("Uses in result")};
  QCOMPARE(model->columnCount(), 6);
  for (int column = 0; column < headers.size(); ++column) {
    QCOMPARE(model->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             headers[column]);
  }
  // Meaning is adjustable just like the other content columns.
  auto* header = table->horizontalHeader();
  for (const int column :
       {pnga::ui::qt::HuffmanInspectorModel::Symbol,
        pnga::ui::qt::HuffmanInspectorModel::Bits,
        pnga::ui::qt::HuffmanInspectorModel::Canonical,
        pnga::ui::qt::HuffmanInspectorModel::ReadOrder,
        pnga::ui::qt::HuffmanInspectorModel::UsesInResult}) {
    QCOMPARE(header->sectionResizeMode(column), QHeaderView::Interactive);
  }
  QCOMPARE(
      header->sectionResizeMode(pnga::ui::qt::HuffmanInspectorModel::Meaning),
      QHeaderView::Interactive);

  struct Cell {
    int row;
    int column;
    const char* text;
  };
  const std::vector<Cell> cells = {
      {0, 0, "65"},
      {0, 1, "literal 65"},
      {0, 2, "3"},
      {0, 3, "100"},
      {0, 4, "001"},
      {0, 5, "2"},
      {1, 0, "268"},
      {1, 1, "length 17-18"},
      {1, 3, "101"},
      {1, 4, "101"},
      {1, 5, "1"},
      {2, 0, "256"},
      {2, 1, "end-of-block"},
  };
  for (const auto& cell : cells) {
    QCOMPARE(model->data(model->index(cell.row, cell.column), Qt::DisplayRole)
                 .toString(),
             QLatin1String(cell.text));
  }

  // Borrowed typed entries back every visible row.
  const auto* first = huffman_model->entryAt(0);
  QVERIFY(first != nullptr);
  QCOMPARE(first->symbol, std::uint16_t{65});
  QCOMPARE(huffman_model->entryAt(3),
           static_cast<const pnga::analysis_engine::HuffmanInspectorEntry*>(
               nullptr));
  for (int row = 0; row < model->rowCount(); ++row) {
    for (int column = 0; column < model->columnCount(); ++column) {
      QVERIFY(table->indexWidget(model->index(row, column)) == nullptr);
    }
  }

  // The Uses column tooltip must declare the bounded scope.
  const QString tooltip = model
                              ->data(model->index(
                                         0, pnga::ui::qt::
                                                HuffmanInspectorModel::
                                                    UsesInResult),
                                     Qt::ToolTipRole)
                              .toString();
  QVERIFY(tooltip.contains(QStringLiteral("bounded")));
  QVERIFY(tooltip.contains(QStringLiteral("Block #7")));
  const QString accessible =
      model->data(model->index(0, 0),
                  pnga::ui::qt::HuffmanAccessibleTextRole)
          .toString();
  QVERIFY(accessible.contains(QStringLiteral("Huffman symbol 65")));
  QVERIFY(accessible.contains(QStringLiteral("literal 65")));
}

void HuffmanInspectorTest::
    zeroBitEntriesHiddenByDefaultAndAvailableOnDemand() {
  pnga::ui::qt::HuffmanInspectorModel model;
  auto table = std::make_shared<const HuffmanInspectorTable>(
      dynamic_view().tables.front());
  model.setTable(table);
  QCOMPARE(model.rowCount(), 3);  // zero-bit symbol 66 hidden by default
  QVERIFY(model.entryAt(2) != nullptr && model.entryAt(2)->symbol == 256);

  model.setHideZeroBitEntries(false);
  QCOMPARE(model.rowCount(), 4);
  const auto* unused = model.entryAt(3);
  QVERIFY(unused != nullptr);
  QCOMPARE(unused->symbol, std::uint16_t{66});
  // The retained zero-bit entry renders an empty-value marker, never a
  // fabricated bit string.
  QCOMPARE(model.data(model.index(3, pnga::ui::qt::HuffmanInspectorModel::
                                         Canonical),
                      Qt::DisplayRole)
               .toString(),
           QStringLiteral("—"));

  model.setHideZeroBitEntries(true);
  QCOMPARE(model.rowCount(), 3);
}

void HuffmanInspectorTest::selectorOrderLabelsAndFiltering() {
  const auto view = dynamic_view();
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* literal_button = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindLiteralLength"));
  auto* distance_button = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindDistance"));
  auto* code_length_button = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindCodeLength"));
  QVERIFY(literal_button != nullptr);
  QVERIFY(distance_button != nullptr);
  QVERIFY(code_length_button != nullptr);
  // Locked labels in the normative order: Literal / Length | Distance |
  // Code Length, with Literal / Length as the default selection.
  QCOMPARE(literal_button->text(), QStringLiteral("Literal / Length"));
  QCOMPARE(distance_button->text(), QStringLiteral("Distance"));
  QCOMPARE(code_length_button->text(), QStringLiteral("Code Length"));
  QVERIFY(literal_button->x() < distance_button->x());
  QVERIFY(distance_button->x() < code_length_button->x());
  QVERIFY(literal_button->isChecked());

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  QCOMPARE(table->model()->rowCount(), 3);

  distance_button->click();
  QCOMPARE(table->model()->rowCount(), 1);
  QCOMPARE(table->model()
               ->data(table->model()->index(0, 0), Qt::DisplayRole)
               .toString(),
           QStringLiteral("0"));

  code_length_button->click();
  QCOMPARE(table->model()->rowCount(), 2);
  QCOMPARE(table->model()
               ->data(table->model()->index(0, 1), Qt::DisplayRole)
               .toString(),
           QStringLiteral("repeat previous length 3-6"));

  // Switching the table keeps the selected Block.
  auto* heading =
      widget.findChild<QLabel*>(QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QVERIFY(heading->text().contains(QStringLiteral("Block #7")));
  QVERIFY(heading->text().contains(QStringLiteral("Dynamic Huffman")));
  literal_button->click();
  QVERIFY(heading->text().contains(QStringLiteral("Block #7")));
}

void HuffmanInspectorTest::detailsContainBothBitOrdersAndProvenance() {
  const auto view = dynamic_view();
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();
  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(0);  // symbol 65
  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Symbol 65")));

  // Both bit orders appear in the details; values stay selectable so they
  // can be copied.
  bool found_canonical = false;
  bool found_read_order = false;
  bool found_provenance = false;
  bool found_occurrences = false;
  bool selectable_value = false;
  const auto labels = widget.findChildren<QLabel*>();
  for (const auto* label : labels) {
    const QString text = label->text();
    if (text.contains(QStringLiteral("100 · 3 bits"))) {
      found_canonical = true;
    }
    if (text.contains(QStringLiteral("001"))) {
      found_read_order = true;
    }
    if (text.contains(QStringLiteral("DEFLATE bits [20, 23)"))) {
      found_provenance = true;
    }
    if (text.contains(QStringLiteral("events #3, #5"))) {
      found_occurrences = true;
    }
    if (text.contains(QStringLiteral("100")) &&
        label->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)) {
      selectable_value = true;
    }
  }
  QVERIFY(found_canonical);
  QVERIFY(found_read_order);
  QVERIFY(found_provenance);
  QVERIFY(found_occurrences);
  QVERIFY(selectable_value);
}

void HuffmanInspectorTest::storedBlockShowsExplicitNoHuffmanState() {
  HuffmanInspectorView view;
  view.status = HuffmanInspectorStatus::kReady;
  view.generation = 5;
  HuffmanInspectorTable stored;
  stored.block_index = 1;
  stored.mode = HuffmanTableMode::kStored;
  stored.selector_label = "LEN/NLEN";
  stored.build_order = 0;
  stored.declared_entry_count = 2;
  stored.bounded_token_count = 3;
  view.tables.push_back(stored);

  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  QCOMPARE(table->model()->rowCount(), 0);
  auto* heading =
      widget.findChild<QLabel*>(QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QCOMPARE(heading->text(), QStringLiteral("Block #1 · Stored"));
  const auto labels = widget.findChildren<QLabel*>();
  bool found_explanation = false;
  for (const auto* label : labels) {
    if (label->text().contains(
            QStringLiteral("Block #1 is stored without Huffman coding.")) &&
        label->text().contains(QStringLiteral(
            "Inspect its LEN/NLEN and byte range in DEFLATE Blocks."))) {
      found_explanation = true;
    }
  }
  QVERIFY(found_explanation);
}

void HuffmanInspectorTest::partialResultRetainsRows() {
  auto view = dynamic_view();
  view.status = HuffmanInspectorStatus::kPartial;
  view.error = "trace token budget exceeded";
  for (auto& table : view.tables) {
    table.truncated = true;
  }
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();
  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  // A partial trace retains the verified rows.
  QCOMPARE(table->model()->rowCount(), 3);
}

void HuffmanInspectorTest::currentAndManualSelectionCoexist() {
  auto view = dynamic_view();
  // The current token's entry keeps the Current highlight.
  view.tables[0].entries[0].selected = true;  // symbol 65
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  CompressionSelectionState state = state_for(3);
  pnga::trace_model::CompressionNavigationTarget manual;
  manual.generation = 3;
  manual.request_serial = 1;
  manual.origin = CompressionNavigationOrigin::kHuffman;
  manual.logical_range =
      DeflateBitRange{DeflateBitOffset{12}, DeflateBitOffset{20}};
  manual.block_index = 7;
  manual.symbol = 268;
  state.manual = manual;
  widget.setSelectionState(state);

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  auto* model =
      qobject_cast<pnga::ui::qt::HuffmanInspectorModel*>(table->model());
  QVERIFY(model != nullptr);
  // Row 0 = symbol 65 (Current), row 1 = symbol 268 (manual selection).
  QCOMPARE(model->data(model->index(0, 0),
                       pnga::ui::qt::HuffmanContainsCurrentRole)
               .toBool(),
           true);
  QVERIFY(model->data(model->index(0, 0), Qt::BackgroundRole).isValid());
  QCOMPARE(model->data(model->index(1, 0),
                       pnga::ui::qt::HuffmanIsManualSelectionRole)
               .toBool(),
           true);
  QCOMPARE(model->data(model->index(0, 0),
                       pnga::ui::qt::HuffmanIsManualSelectionRole)
               .toBool(),
           false);
  table->selectRow(1);
  QVERIFY(table->selectionModel()->isRowSelected(1, QModelIndex()));
  QCOMPARE(model->data(model->index(0, 0),
                       pnga::ui::qt::HuffmanContainsCurrentRole)
               .toBool(),
           true);

  // A state from another generation highlights nothing.
  CompressionSelectionState stale = state_for(99);
  stale.manual = manual;
  widget.setSelectionState(stale);
  QCOMPARE(model->data(model->index(1, 0),
                       pnga::ui::qt::HuffmanIsManualSelectionRole)
               .toBool(),
           false);
}

void HuffmanInspectorTest::rowSelectionPublishesManualTarget() {
  const auto view = dynamic_view();
  pnga::ui::qt::CompressionSelectionStore store;
  store.resetGeneration(3);
  pnga::ui::qt::HuffmanInspector widget;
  widget.setSelectionStore(&store);
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(0);  // symbol 65

  QVERIFY(store.state().manual.has_value());
  QCOMPARE(store.state().manual->origin,
           CompressionNavigationOrigin::kHuffman);
  QCOMPARE(store.state().manual->block_index,
           std::optional<std::uint64_t>{7});
  QCOMPARE(store.state().manual->symbol, std::optional<std::uint16_t>{65});
  QVERIFY(store.state().manual->valid());
  // Row selection never records history and never navigates.
  QCOMPARE(store.history().size(), std::size_t{0});
  QVERIFY(store.state().manual->request_serial != 0);

  // Without a store the same typed target is emitted through the signal.
  pnga::ui::qt::HuffmanInspector standalone;
  standalone.setView(view);
  standalone.show();
  QCoreApplication::processEvents();
  QSignalSpy spy(&standalone,
                 &pnga::ui::qt::HuffmanInspector::navigationRequested);
  QVERIFY(spy.isValid());
  auto* standalone_table = huffmanTable(standalone);
  QVERIFY(standalone_table != nullptr);
  standalone_table->selectRow(0);
  QCOMPARE(spy.count(), 1);
  const auto target = spy.front()
                          .front()
                          .value<pnga::trace_model::
                                     CompressionNavigationTarget>();
  QCOMPARE(target.origin, CompressionNavigationOrigin::kHuffman);
  QCOMPARE(target.symbol, std::optional<std::uint16_t>{65});
  QCOMPARE(target.block_index, std::optional<std::uint64_t>{7});
  QVERIFY(target.valid());
}

void HuffmanInspectorTest::openOccurrenceNavigatesBoundedTokens() {
  const auto view = dynamic_view();
  pnga::ui::qt::CompressionSelectionStore store;
  store.resetGeneration(3);
  pnga::ui::qt::HuffmanInspector widget;
  widget.setSelectionStore(&store);
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(0);  // symbol 65 with occurrences #3 and #5
  auto* open = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanOpenOccurrence"));
  QVERIFY(open != nullptr);
  open->click();
  QCOMPARE(store.history().size(), std::size_t{1});
  const auto& first = store.history().front();
  QCOMPARE(first.origin, CompressionNavigationOrigin::kHuffman);
  QCOMPARE(first.token_index, std::optional<std::uint64_t>{3});
  QCOMPARE(first.symbol, std::optional<std::uint16_t>{65});
  QCOMPARE(first.block_index, std::optional<std::uint64_t>{7});
  QVERIFY((std::get<DeflateBitRange>(first.logical_range) ==
           DeflateBitRange{DeflateBitOffset{4}, DeflateBitOffset{12}}));
  QCOMPARE(first.physical_spans.size(), std::size_t{1});
  QVERIFY((first.physical_spans.front() ==
           pnga::trace_model::FileByteRange{
               pnga::trace_model::FileByteOffset{100},
               pnga::trace_model::FileByteOffset{110}}));

  open->click();
  QCOMPARE(store.history().size(), std::size_t{2});
  QCOMPARE(store.history().back().token_index,
           std::optional<std::uint64_t>{5});
  QVERIFY((std::get<DeflateBitRange>(
              store.history().back().logical_range) ==
           DeflateBitRange{DeflateBitOffset{20}, DeflateBitOffset{28}}));

  // The bounded occurrence list cycles; no separate index is created.
  open->click();
  QCOMPARE(store.history().size(), std::size_t{3});
  QCOMPARE(store.history().back().token_index,
           std::optional<std::uint64_t>{3});

  // Back returns to the earlier occurrence and its symbol.
  QVERIFY(store.goBack());
  QCOMPARE(store.state().manual->token_index,
           std::optional<std::uint64_t>{5});
  QCOMPARE(store.state().manual->symbol, std::optional<std::uint16_t>{65});
  QVERIFY(table->selectionModel()->hasSelection());
}

void HuffmanInspectorTest::
    openOccurrenceWithoutOccurrencesShowsExactText() {
  const auto view = dynamic_view();
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QCoreApplication::processEvents();

  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  // The Distance table's only symbol has no occurrence in the bounded
  // result.
  auto* distance_button = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindDistance"));
  QVERIFY(distance_button != nullptr);
  distance_button->click();
  table->selectRow(0);  // distance symbol 0, never consumed by the result
  auto* open = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanOpenOccurrence"));
  QVERIFY(open != nullptr);
  open->click();

  const auto labels = widget.findChildren<QLabel*>();
  bool found = false;
  for (const auto* label : labels) {
    if (label->text() ==
        QStringLiteral("This symbol is defined but not used by Block #7.")) {
      found = true;
    }
  }
  QVERIFY(found);
}

void HuffmanInspectorTest::keyboardNavigationMovesRowSelection() {
  const auto view = dynamic_view();
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  table->setFocus();
  table->selectRow(0);

  QTest::keyClick(table, Qt::Key_Down);
  QVERIFY(table->selectionModel()->hasSelection());
  QCOMPARE(table->currentIndex().row(), 1);
  QTest::keyClick(table, Qt::Key_Down);
  QCOMPARE(table->currentIndex().row(), 2);
  QTest::keyClick(table, Qt::Key_End);
  QCOMPARE(table->currentIndex().row(), 2);
  QTest::keyClick(table, Qt::Key_Up);
  QCOMPARE(table->currentIndex().row(), 1);
  QTest::keyClick(table, Qt::Key_Home);
  QCOMPARE(table->currentIndex().row(), 0);
}

void HuffmanInspectorTest::sameGenerationRepublishPreservesManualWidths() {
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(dynamic_view());
  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  // A manual width adjustment survives a same-generation republish (row
  // publish, Current change, selection change).
  table->setColumnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol, 200);
  widget.setView(dynamic_view());
  QCOMPARE(table->columnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol),
           200);
}

void HuffmanInspectorTest::generationChangeAndKindSwitchRefitColumns() {
  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(dynamic_view());
  auto* table = huffmanTable(widget);
  QVERIFY(table != nullptr);
  table->setColumnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol, 200);
  // A document open publishes a new generation: the widths re-derive from
  // content into the normative fresh-open geometry.
  auto next_document = dynamic_view();
  next_document.generation = 9;
  widget.setView(next_document);
  pnga::ui::qt::HuffmanInspector reference;
  reference.setView(dynamic_view());
  auto* reference_table = huffmanTable(reference);
  QVERIFY(reference_table != nullptr);
  QCOMPARE(table->columnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol),
           reference_table->columnWidth(
               pnga::ui::qt::HuffmanInspectorModel::Symbol));
  // A table-kind switch replaces the projected content entirely: the widths
  // re-derive from the new table's content.
  table->setColumnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol, 200);
  auto* distance_button = widget.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindDistance"));
  QVERIFY(distance_button != nullptr);
  distance_button->click();
  auto* reference_distance = reference.findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindDistance"));
  QVERIFY(reference_distance != nullptr);
  reference_distance->click();
  QCOMPARE(table->columnWidth(pnga::ui::qt::HuffmanInspectorModel::Symbol),
           reference_table->columnWidth(
               pnga::ui::qt::HuffmanInspectorModel::Symbol));
}

QTEST_MAIN(HuffmanInspectorTest)
#include "huffman_inspector_test.moc"
