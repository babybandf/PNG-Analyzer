#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>

#include <QtTest/QtTest>

#include <QLabel>
#include <QColor>
#include <QPushButton>
#include <QTableWidget>

class DecodeTraceInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersMatchArithmeticAndNavigation();
};

void DecodeTraceInspectorTest::rendersMatchArithmeticAndNavigation() {
  pnga::analysis_engine::DecodeTraceInspectorView view;
  view.status = pnga::analysis_engine::DecodeTraceInspectorStatus::kReady;
  view.selected_token_index = 2;
  view.selected_output_offset = 5;
  pnga::analysis_engine::DecodeTraceStep step;
  step.token_index = 2;
  step.path = pnga::analysis_engine::DecodeTracePath::kMatch;
  step.input_bit_begin = 17;
  step.input_bit_end = 31;
  step.output_begin = 1;
  step.output_end = 13;
  step.huffman_symbol = 260;
  step.length = 12;
  step.length_base = 11;
  step.length_extra_bits = 1;
  step.length_extra_value = 1;
  step.distance = 7;
  step.distance_base = 5;
  step.distance_extra_bits = 1;
  step.distance_extra_value = 2;
  step.selected = true;
  step.selected_output_byte = 5;
  step.match_source_ranges.push_back({0, 7, 0});
  view.steps.push_back(step);

  pnga::ui::qt::DecodeTraceInspector widget;
  widget.setView(view);
  auto* table = widget.findChild<QTableWidget*>(
      QStringLiteral("decodeTraceInspectorTable"));
  QVERIFY(table != nullptr);
  QCOMPARE(table->rowCount(), 1);
  // Token | Path | Input bits | Output bytes
  QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2"));
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("match"));
  QCOMPARE(table->item(0, 2)->text(), QStringLiteral("17..31"));
  QCOMPARE(table->item(0, 3)->text(), QStringLiteral("1..13"));
  QCOMPARE(table->item(0, 0)->background().color(),
           QColor(QStringLiteral("#FFF4CC")));

  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Token #2")));
  QVERIFY(details_title->text().contains(QStringLiteral("match")));
  bool found_length = false;
  bool found_source = false;
  bool found_input = false;
  const auto labels = widget.findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->text().contains(QStringLiteral("12 = base 11 + extra 1"))) {
      found_length = true;
    }
    if (label->text().contains(QStringLiteral("[0..7)"))) {
      found_source = true;
    }
    if (label->text().contains(QStringLiteral("Deflate bits [17, 31)"))) {
      found_input = true;
    }
  }
  QVERIFY(found_length);
  QVERIFY(found_source);
  QVERIFY(found_input);

  QSignalSpy hex_spy(&widget,
                     &pnga::ui::qt::DecodeTraceInspector::showInHexRequested);
  QSignalSpy deflate_spy(
      &widget, &pnga::ui::qt::DecodeTraceInspector::showInDeflateRequested);
  const auto buttons = widget.findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      button->click();
    }
  }
  QCOMPARE(hex_spy.count(), 1);
  QCOMPARE(hex_spy.takeFirst().at(0).toULongLong(), qulonglong{1});
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in DEFLATE")) {
      button->click();
    }
  }
  QCOMPARE(deflate_spy.count(), 1);
  QCOMPARE(deflate_spy.takeFirst().at(0).toULongLong(), qulonglong{17});
}

QTEST_MAIN(DecodeTraceInspectorTest)
#include "decode_trace_inspector_test.moc"
