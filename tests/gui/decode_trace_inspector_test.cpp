// WP-5U12E Decode Trace page tests: the model-backed QTableView renders the
// bounded semantic steps with the exact Current | Step | Input bits | Event |
// Output columns and no QTableWidget or per-row index widgets; the scope
// heading identifies the bounded result; details expose every Match field as
// structured projection facts; Current and Manual Selection coexist;
// Partial/Error rows are retained; row selection publishes a typed Manual
// target only; Show in Hex carries the compressed DeflateBitRange with every
// physical file span while Show inflated output carries only the
// InflatedByteRange; keyboard navigation and accessible text follow flow-ui
// sections 14 and 20.

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_model.h>

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
#include <variant>
#include <vector>

namespace {

using pnga::analysis_engine::DecodeTraceInspectorView;
using pnga::analysis_engine::DecodeTracePath;
using pnga::analysis_engine::DecodeTraceScope;
using pnga::analysis_engine::DecodeTraceStep;
using pnga::deflate_trace::TokenKind;
using pnga::deflate_trace::TokenOutputRange;
using pnga::trace_model::CompressionNavigationOrigin;
using pnga::trace_model::CompressionSelectionState;
using pnga::trace_model::DeflateBitOffset;
using pnga::trace_model::DeflateBitRange;
using pnga::trace_model::FileByteOffset;
using pnga::trace_model::FileByteRange;
using pnga::trace_model::InflatedByteOffset;
using pnga::trace_model::InflatedByteRange;

QTableView* traceTable(pnga::ui::qt::DecodeTraceInspector& widget) {
  return widget.findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
}

DecodeTraceStep literal_step(std::uint64_t index, std::uint8_t value) {
  DecodeTraceStep step;
  step.token_index = index;
  step.block_index = 0;
  step.path = DecodeTracePath::kLiteral;
  step.input_range = DeflateBitRange{DeflateBitOffset{10 + 8 * index},
                                     DeflateBitOffset{18 + 8 * index}};
  step.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{43}, FileByteOffset{44}});
  step.output_range = InflatedByteRange{InflatedByteOffset{index},
                                        InflatedByteOffset{index + 1}};
  step.event_text = "Literal 0x41";
  step.huffman_symbol = value;
  step.literal = value;
  return step;
}

DecodeTraceStep match_step(std::uint64_t index) {
  DecodeTraceStep step;
  step.token_index = index;
  step.block_index = 0;
  step.path = DecodeTracePath::kMatch;
  step.input_range = DeflateBitRange{DeflateBitOffset{922},
                                     DeflateBitOffset{937}};
  // The token input crosses an IDAT boundary: two ordered file spans.
  step.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{43}, FileByteOffset{44}});
  step.physical_input_spans.push_back(
      FileByteRange{FileByteOffset{56}, FileByteOffset{57}});
  step.output_range = InflatedByteRange{InflatedByteOffset{1569},
                                        InflatedByteOffset{1587}};
  step.event_text = "Match len 18 / dist 7";
  step.huffman_symbol = 268;
  step.length = 18;
  step.distance = 7;
  step.length_base = 17;
  step.length_extra_bits = 1;
  step.length_extra_value = 1;
  step.distance_base = 7;
  step.distance_extra_bits = 1;
  step.distance_extra_value = 0;
  step.match_source_ranges.push_back(TokenOutputRange{1562, 1580, 34});
  step.match_target = step.output_range;
  step.match_overlaps = true;
  return step;
}

DecodeTraceStep eob_step(std::uint64_t index) {
  DecodeTraceStep step;
  step.token_index = index;
  step.block_index = 0;
  step.path = DecodeTracePath::kEndOfBlock;
  step.input_range = DeflateBitRange{DeflateBitOffset{937},
                                     DeflateBitOffset{944}};
  step.output_range = InflatedByteRange{};
  step.event_text = "End of block";
  step.huffman_symbol = 256;
  return step;
}

DecodeTraceInspectorView ready_view() {
  DecodeTraceInspectorView view;
  view.scope.generation = 6;
  view.scope.requested_output = InflatedByteRange{InflatedByteOffset{1568},
                                                  InflatedByteOffset{1587}};
  view.scope.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  view.scope.returned_token_count = 3;
  // Legacy WP-505C sync fields stay consistent with the scope.
  view.status = pnga::analysis_engine::DecodeTraceInspectorStatus::kReady;
  view.generation = 6;
  view.steps.push_back(literal_step(35, 0x41));
  view.steps.push_back(match_step(36));
  view.steps.push_back(eob_step(37));
  return view;
}

}  // namespace

class DecodeTraceInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void modelRendersTypedRowsWithExactHeaders();
  void scopeHeadingIdentifiesBoundedResult();
  void detailsExposeEveryMatchField();
  void literalAndEobDetailsStayStructured();
  void currentAndManualSelectionCoexist();
  void partialAndErrorRowsAreRetained();
  void rowSelectionPublishesTypedManualTargetOnly();
  void showInHexSendsTypedCompressedTarget();
  void showInflatedOutputSendsTypedOutputTarget();
  void keyboardNavigationMovesRowSelection();
};

void DecodeTraceInspectorTest::initTestCase() {
  // The Current-row background uses the centralized theme token; the theme
  // is installed (without persisting settings) so the color tokens resolve
  // on the offscreen platform as well.
  auto* theme = new pnga::ui::qt::ApplicationTheme(qApp, this);
  theme->setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                 /*persist=*/false);
}

void DecodeTraceInspectorTest::modelRendersTypedRowsWithExactHeaders() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  widget.show();
  QCoreApplication::processEvents();

  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  // Model/view contract: a QTableView over a QAbstractTableModel and no
  // QTableWidget or per-row index widget anywhere on the page.
  QVERIFY(widget.findChild<QTableWidget*>() == nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  auto* trace_model = qobject_cast<pnga::ui::qt::DecodeTraceModel*>(model);
  QVERIFY(trace_model != nullptr);

  QCOMPARE(model->rowCount(), 3);
  const QStringList headers = {QStringLiteral("Current"),
                               QStringLiteral("Step"),
                               QStringLiteral("Input bits"),
                               QStringLiteral("Event"),
                               QStringLiteral("Output")};
  QCOMPARE(model->columnCount(), 5);
  for (int column = 0; column < headers.size(); ++column) {
    QCOMPARE(model->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             headers[column]);
  }

  struct Cell {
    int row;
    int column;
    QString text;
  };
  const std::vector<Cell> cells = {
      {0, 1, QStringLiteral("35")},
      {0, 2, QStringLiteral("290–298")},
      {0, 3, QStringLiteral("Literal 0x41")},
      {0, 4, QStringLiteral("35")},
      {1, 1, QStringLiteral("36")},
      {1, 2, QStringLiteral("922–937")},
      {1, 3, QStringLiteral("Match len 18 / dist 7")},
      {1, 4, QStringLiteral("1569–1587")},
      {2, 1, QStringLiteral("37")},
      {2, 2, QStringLiteral("937–944")},
      {2, 3, QStringLiteral("End of block")},
      {2, 4, QStringLiteral("—")},
  };
  for (const auto& cell : cells) {
    QCOMPARE(model->data(model->index(cell.row, cell.column), Qt::DisplayRole)
                 .toString(),
             cell.text);
  }

  // Borrowed typed steps back every row.
  const auto* first = trace_model->stepAt(0);
  QVERIFY(first != nullptr);
  QCOMPARE(first->token_index, std::uint64_t{35});
  QCOMPARE(trace_model->stepAt(3),
           static_cast<const pnga::analysis_engine::DecodeTraceStep*>(
               nullptr));
  for (int row = 0; row < model->rowCount(); ++row) {
    for (int column = 0; column < model->columnCount(); ++column) {
      QVERIFY(table->indexWidget(model->index(row, column)) == nullptr);
    }
  }

  const QString accessible =
      model->data(model->index(1, 0), pnga::ui::qt::DecodeTraceAccessibleTextRole)
          .toString();
  QVERIFY(accessible.contains(QStringLiteral("Step 36")));
  QVERIFY(accessible.contains(QStringLiteral("DEFLATE bits 922 to 937")));
  QVERIFY(accessible.contains(QStringLiteral("Match len 18 / dist 7")));
  QVERIFY(accessible.contains(QStringLiteral("inflated output bytes 1569 to 1587")));
}

void DecodeTraceInspectorTest::scopeHeadingIdentifiesBoundedResult() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* heading = widget.findChild<QLabel*>(
      QStringLiteral("decodeTraceScopeHeading"));
  QVERIFY(heading != nullptr);
  // Title facts: bounded output range, returned token count and status;
  // never a whole-stream claim.
  QVERIFY(heading->text().contains(QStringLiteral("output bytes 1568–1587")));
  QVERIFY(heading->text().contains(QStringLiteral("3 events")));
  QVERIFY(heading->text().contains(QStringLiteral("ready")));

  // Truncation and the stop reason are part of the scope facts.
  auto truncated = ready_view();
  truncated.scope.truncated = true;
  truncated.scope.status =
      pnga::analysis_engine::TraceQueryStatus::kPartial;
  truncated.scope.stop_reason = "trace token budget exceeded";
  widget.setView(truncated);
  QVERIFY(heading->text().contains(QStringLiteral("truncated")));
  QVERIFY(heading->text().contains(
      QStringLiteral("trace token budget exceeded")));
}

void DecodeTraceInspectorTest::detailsExposeEveryMatchField() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(1);

  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Event #36")));
  QVERIFY(details_title->text().contains(QStringLiteral("match")));

  // Every Match detail field is rendered from the structured projection.
  const QString expected[] = {
      QStringLiteral("base 17 + extra 1 (1 bit) = 18"),
      QStringLiteral("base 7 + extra 0 (1 bit) = 7"),
      QStringLiteral("[1562, 1580) token 34"),
      QStringLiteral("Inflated bytes [1569, 1587)"),
      QStringLiteral("yes"),
      QStringLiteral("DEFLATE bits 922–937"),
  };
  const auto labels = widget.findChildren<QLabel*>();
  for (const auto& text : expected) {
    bool found = false;
    for (const auto* label : labels) {
      if (label->text().contains(text)) {
        found = true;
        break;
      }
    }
    QVERIFY2(found, qPrintable(QStringLiteral("missing detail: %1").arg(text)));
  }

  // Detail values are selectable so they can be copied.
  auto* body = widget.findChild<QWidget*>(
      QStringLiteral("compressionDetailsBody"));
  QVERIFY(body != nullptr);
  bool selectable_value = false;
  const auto body_labels = body->findChildren<QLabel*>();
  for (const auto* label : body_labels) {
    if (label->text().contains(QStringLiteral("base 17 + extra 1"))) {
      selectable_value = label->textInteractionFlags().testFlag(
          Qt::TextSelectableByMouse);
    }
  }
  QVERIFY(selectable_value);
}

void DecodeTraceInspectorTest::literalAndEobDetailsStayStructured() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);

  table->selectRow(0);
  const auto labels = widget.findChildren<QLabel*>();
  bool literal_hex = false;
  bool symbol = false;
  for (const auto* label : labels) {
    if (label->text() == QStringLiteral("0x41")) {
      literal_hex = true;
    }
    if (label->text().contains(QStringLiteral("symbol 65"))) {
      symbol = true;
    }
  }
  QVERIFY(literal_hex);
  QVERIFY(symbol);

  // The end-of-block event has no output and no match arithmetic.
  table->selectRow(2);
  bool end_of_block = false;
  for (const auto* label : widget.findChildren<QLabel*>()) {
    if (label->text() == QStringLiteral("End of block")) {
      end_of_block = true;
    }
  }
  QVERIFY(end_of_block);
}

void DecodeTraceInspectorTest::currentAndManualSelectionCoexist() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);

  // A committed pixel marks the step that contains the current output byte
  // without touching the manual selection.
  CompressionSelectionState state;
  state.generation = 6;
  pnga::trace_model::CompressionCurrentMapping current;
  current.generation = 6;
  current.source_unit = pnga::trace_model::DocumentSourceUnit{};
  current.output_range = InflatedByteRange{InflatedByteOffset{1573},
                                           InflatedByteOffset{1574}};
  current.block_index = 0;
  state.current = current;
  widget.setSelectionState(state);

  QVERIFY(model->data(model->index(1, 0),
                      pnga::ui::qt::DecodeTraceContainsCurrentRole)
              .toBool());
  QCOMPARE(model->data(model->index(1, 0), Qt::DisplayRole).toString(),
           QStringLiteral("●"));
  const QColor current_background =
      model->data(model->index(1, 1), Qt::BackgroundRole)
          .value<QColor>();
  QCOMPARE(current_background,
           pnga::ui::qt::ApplicationTheme::applicationColor(
               pnga::ui::qt::ApplicationTheme::ColorToken::kCurrentPixel));

  // A manual row selection coexists with the Current marker.
  table->selectRow(0);
  QVERIFY(table->selectionModel()->isRowSelected(0, QModelIndex()));
  QVERIFY(model->data(model->index(1, 0),
                      pnga::ui::qt::DecodeTraceContainsCurrentRole)
              .toBool());

  // A state from another generation highlights nothing.
  CompressionSelectionState stale;
  stale.generation = 5;
  widget.setSelectionState(stale);
  QVERIFY(!model->data(model->index(1, 0),
                       pnga::ui::qt::DecodeTraceContainsCurrentRole)
               .toBool());
}

void DecodeTraceInspectorTest::partialAndErrorRowsAreRetained() {
  auto partial = ready_view();
  partial.scope.status = pnga::analysis_engine::TraceQueryStatus::kPartial;
  partial.scope.truncated = true;
  partial.scope.stop_reason = "trace token budget exceeded";
  partial.steps.pop_back();
  partial.scope.returned_token_count = 2;

  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(partial);
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  // Verified rows stay browsable beside the stop reason.
  QCOMPARE(table->model()->rowCount(), 2);
  auto* heading = widget.findChild<QLabel*>(
      QStringLiteral("decodeTraceScopeHeading"));
  QVERIFY(heading != nullptr);
  QVERIFY(heading->text().contains(QStringLiteral("partial")));
  QVERIFY(heading->text().contains(
      QStringLiteral("trace token budget exceeded")));

  // An error result keeps the verified rows and names the stop reason in
  // the details instead of an empty table.
  auto error = ready_view();
  error.scope.status = pnga::analysis_engine::TraceQueryStatus::kError;
  error.scope.stop_reason = "invalid distance code";
  error.steps.pop_back();
  error.steps.pop_back();
  error.scope.returned_token_count = 1;
  widget.setView(error);
  QCOMPARE(table->model()->rowCount(), 1);
  bool found_stop = false;
  for (const auto* label : widget.findChildren<QLabel*>()) {
    if (label->text().contains(QStringLiteral("invalid distance code"))) {
      found_stop = true;
    }
  }
  QVERIFY(found_stop);
}

void DecodeTraceInspectorTest::rowSelectionPublishesTypedManualTargetOnly() {
  pnga::ui::qt::CompressionSelectionStore store;
  store.resetGeneration(6);
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setSelectionStore(&store);
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);

  QSignalSpy navigation_spy(
      &store, &pnga::ui::qt::CompressionSelectionStore::navigationRequested);
  QVERIFY(navigation_spy.isValid());
  table->selectRow(0);

  // Row selection changes only Manual Selection: no history entry, no
  // navigation request and no trace submission.
  QVERIFY(store.state().manual.has_value());
  QCOMPARE(store.state().manual->token_index,
           std::optional<std::uint64_t>{35});
  QCOMPARE(store.state().manual->origin,
           CompressionNavigationOrigin::kDecodeTrace);
  QCOMPARE(store.history().size(), std::size_t{0});
  QCOMPARE(navigation_spy.count(), 0);

  // The typed manual target carries the compressed input range and spans.
  const auto logical = std::get_if<pnga::trace_model::DeflateBitRange>(
      &store.state().manual->logical_range);
  QVERIFY(logical != nullptr);
  QCOMPARE(logical->begin, DeflateBitOffset{10 + 8 * 35});
  QCOMPARE(store.state().manual->physical_spans.size(), std::size_t{1});
}

void DecodeTraceInspectorTest::showInHexSendsTypedCompressedTarget() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(1);

  QSignalSpy hex_spy(
      &widget, &pnga::ui::qt::DecodeTraceInspector::navigationRequested);
  QVERIFY(hex_spy.isValid());
  auto* hex_button =
      widget.findChild<QPushButton*>(QStringLiteral("decodeShowInHex"));
  QVERIFY(hex_button != nullptr);
  QCOMPARE(hex_button->text(), QStringLiteral("Show in Hex"));
  QVERIFY(hex_button->isEnabled());
  hex_button->click();

  // The compressed-input action is one typed navigation with the precise
  // DeflateBitRange and every physical file span.
  QCOMPARE(hex_spy.count(), 1);
  const auto target =
      hex_spy.takeFirst().at(0).value<pnga::trace_model::CompressionNavigationTarget>();
  QVERIFY(target.valid());
  QCOMPARE(target.origin, CompressionNavigationOrigin::kDecodeTrace);
  const auto* bits = std::get_if<pnga::trace_model::DeflateBitRange>(
      &target.logical_range);
  QVERIFY(bits != nullptr);
  QCOMPARE(bits->begin, DeflateBitOffset{922});
  QCOMPARE(bits->end, DeflateBitOffset{937});
  QCOMPARE(target.physical_spans.size(), std::size_t{2});
  QCOMPARE(target.physical_spans.front(),
           (FileByteRange{FileByteOffset{43}, FileByteOffset{44}}));
  QCOMPARE(target.physical_spans.back(),
           (FileByteRange{FileByteOffset{56}, FileByteOffset{57}}));
  QCOMPARE(target.token_index, std::optional<std::uint64_t>{36});
  QCOMPARE(target.block_index, std::optional<std::uint64_t>{0});

  // No legacy untyped integer signal exists on the page anymore.
  const QMetaObject* meta = widget.metaObject();
  for (int i = 0; i < meta->methodCount(); ++i) {
    const QMetaMethod method = meta->method(i);
    if (method.methodType() == QMetaMethod::Signal) {
      QVERIFY(!QString::fromLatin1(method.name())
                   .startsWith(QStringLiteral("showIn")));
    }
  }
}

void DecodeTraceInspectorTest::showInflatedOutputSendsTypedOutputTarget() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  table->selectRow(1);

  QSignalSpy inflated_spy(
      &widget, &pnga::ui::qt::DecodeTraceInspector::navigationRequested);
  QVERIFY(inflated_spy.isValid());
  auto* inflated_button = widget.findChild<QPushButton*>(
      QStringLiteral("decodeShowInflatedOutput"));
  QVERIFY(inflated_button != nullptr);
  QCOMPARE(inflated_button->text(), QStringLiteral("Show inflated output"));
  QVERIFY(inflated_button->isEnabled());
  inflated_button->click();

  // The output action carries only the InflatedByteRange and no compressed
  // scalar or physical span.
  QCOMPARE(inflated_spy.count(), 1);
  const auto target = inflated_spy.takeFirst()
                          .at(0)
                          .value<pnga::trace_model::CompressionNavigationTarget>();
  QVERIFY(target.valid());
  const auto* bytes = std::get_if<pnga::trace_model::InflatedByteRange>(
      &target.logical_range);
  QVERIFY(bytes != nullptr);
  QCOMPARE(bytes->begin, InflatedByteOffset{1569});
  QCOMPARE(bytes->end, InflatedByteOffset{1587});
  QVERIFY(target.physical_spans.empty());
  QCOMPARE(target.token_index, std::optional<std::uint64_t>{36});
}

void DecodeTraceInspectorTest::keyboardNavigationMovesRowSelection() {
  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(ready_view());
  widget.resize(600, 800);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  auto* table = traceTable(widget);
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);

  table->selectRow(0);
  QTest::keyClick(table, Qt::Key_Down);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 1);
  QTest::keyClick(table, Qt::Key_Down);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 2);
  QTest::keyClick(table, Qt::Key_Up);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 1);
  QTest::keyClick(table, Qt::Key_End);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 2);
  QTest::keyClick(table, Qt::Key_Home);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 0);
  QTest::keyClick(table, Qt::Key_PageDown);
  QVERIFY(table->selectionModel()->currentIndex().row() > 0);
  QTest::keyClick(table, Qt::Key_PageUp);
  QCOMPARE(table->selectionModel()->currentIndex().row(), 0);
}

QTEST_MAIN(DecodeTraceInspectorTest)
#include "decode_trace_inspector_test.moc"
