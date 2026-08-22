#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QTableWidget>

class HuffmanInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersDynamicEntryAndSelection();
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
  QCOMPARE(table_widget->item(0, 1)->text(), QStringLiteral("dynamic"));
  QCOMPARE(table_widget->item(0, 4)->text(), QStringLiteral("65"));
  QVERIFY(table_widget->item(0, 7)->text().contains(QStringLiteral("selected")));
}

QTEST_MAIN(HuffmanInspectorTest)
#include "huffman_inspector_test.moc"
