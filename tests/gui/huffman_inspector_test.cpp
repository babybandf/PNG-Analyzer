#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QTableWidget>

class HuffmanInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersDynamicEntryAndSelection();
  void selectorFiltersByTableKind();
  void rendersStoredExplanation();
};

void HuffmanInspectorTest::rendersDynamicEntryAndSelection() {
  pnga::analysis_engine::HuffmanInspectorView view;
  view.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  view.generation = 3;
  view.selected_token_index = 9;
  view.selected_input_bit_begin = 21;
  view.selected_input_bit_end = 28;
  pnga::analysis_engine::HuffmanInspectorTable table;
  table.block_index = 1;
  table.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  table.build_order = 1;
  table.entries.push_back({65, 7, 42, 5, 12, true});
  table.declared_entry_count = 1;
  view.tables.push_back(table);

  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  auto* table_widget = widget.findChild<QTableWidget*>(
      QStringLiteral("huffmanInspectorTable"));
  QVERIFY(table_widget != nullptr);
  QCOMPARE(table_widget->rowCount(), 1);
  // Current | Build | Symbol | Bits | Canonical | Definition bits
  QCOMPARE(table_widget->item(0, 0)->text(), QStringLiteral("●"));
  QCOMPARE(table_widget->item(0, 1)->text(), QStringLiteral("1"));
  QCOMPARE(table_widget->item(0, 2)->text(), QStringLiteral("65"));
  QCOMPARE(table_widget->item(0, 3)->text(), QStringLiteral("7"));
  QCOMPARE(table_widget->item(0, 4)->text(), QStringLiteral("42 (7 bits)"));
  QCOMPARE(table_widget->item(0, 5)->text(), QStringLiteral("5..12"));

  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Symbol 65")));
  QVERIFY(details_title->text().contains(QStringLiteral("details")));
}

void HuffmanInspectorTest::selectorFiltersByTableKind() {
  pnga::analysis_engine::HuffmanInspectorView view;
  view.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  pnga::analysis_engine::HuffmanInspectorTable literal;
  literal.block_index = 1;
  literal.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  literal.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  literal.build_order = 1;
  literal.entries.push_back({65, 7, 42, 5, 12, false});
  literal.declared_entry_count = 1;
  pnga::analysis_engine::HuffmanInspectorTable distance;
  distance.block_index = 1;
  distance.mode = pnga::analysis_engine::HuffmanTableMode::kDynamic;
  distance.kind = pnga::deflate_trace::HuffmanTableKind::kDistance;
  distance.build_order = 2;
  distance.entries.push_back({0, 5, 3, 7, 10, false});
  distance.declared_entry_count = 1;
  view.tables.push_back(literal);
  view.tables.push_back(distance);

  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  auto* table_widget = widget.findChild<QTableWidget*>(
      QStringLiteral("huffmanInspectorTable"));
  QVERIFY(table_widget != nullptr);
  auto* selector = widget.findChild<QComboBox*>(
      QStringLiteral("huffmanTableKindSelector"));
  QVERIFY(selector != nullptr);
  QCOMPARE(selector->currentIndex(), 1);  // Literal / Length default

  // Literal/Length selected -> one literal entry.
  QCOMPARE(table_widget->rowCount(), 1);
  QCOMPARE(table_widget->item(0, 2)->text(), QStringLiteral("65"));

  // Distance selected -> one distance entry; switching emits no replay request
  // (the widget only refilters the already-published bounded tables).
  selector->setCurrentIndex(2);
  QCOMPARE(table_widget->rowCount(), 1);
  QCOMPARE(table_widget->item(0, 2)->text(), QStringLiteral("0"));

  // Code length selected -> no code-length table in the bounded result.
  selector->setCurrentIndex(0);
  QCOMPARE(table_widget->rowCount(), 0);
  auto* heading =
      widget.findChild<QLabel*>(QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QVERIFY(heading->text().contains(QStringLiteral("No Code length")));
}

void HuffmanInspectorTest::rendersStoredExplanation() {
  pnga::analysis_engine::HuffmanInspectorView view;
  view.status = pnga::analysis_engine::HuffmanInspectorStatus::kReady;
  pnga::analysis_engine::HuffmanInspectorTable table;
  table.block_index = 0;
  table.mode = pnga::analysis_engine::HuffmanTableMode::kStored;
  table.build_order = 0;
  table.declared_entry_count = 2;  // LEN, NLEN
  view.tables.push_back(table);

  pnga::ui::qt::HuffmanInspector widget;
  widget.setView(view);
  auto* table_widget = widget.findChild<QTableWidget*>(
      QStringLiteral("huffmanInspectorTable"));
  QVERIFY(table_widget != nullptr);
  QCOMPARE(table_widget->rowCount(), 1);
  QCOMPARE(table_widget->item(0, 2)->text(), QStringLiteral("LEN/NLEN"));
  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Stored")));
}

QTEST_MAIN(HuffmanInspectorTest)
#include "huffman_inspector_test.moc"
