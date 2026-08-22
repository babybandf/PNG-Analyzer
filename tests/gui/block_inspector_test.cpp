#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/ui/qt/block_inspector.h>

#include <QtTest/QtTest>

#include <QPushButton>
#include <QTableWidget>

class BlockInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void rendersViewAndExposesNavigationSignals();
};

void BlockInspectorTest::rendersViewAndExposesNavigationSignals() {
  pnga::analysis_engine::BlockInspectorView view;
  view.status = pnga::analysis_engine::BlockInspectorStatus::kReady;
  view.generation = 11;
  view.scanline = 4;
  view.selected_output_offset = 12;
  view.selected_block_index = 2;
  pnga::analysis_engine::BlockInspectorRow row;
  row.block_index = 2;
  row.type = pnga::deflate_index::BlockType::kStored;
  row.input_bit_begin = 16;
  row.input_bit_end = 32;
  row.output_begin = 8;
  row.output_end = 20;
  row.current_output_position = 12;
  row.physical_spans.push_back(
      {pnga::trace_model::ProvenanceSpace::kPhysicalFile, 100, 3, 0, 0,
       false});
  view.rows.push_back(row);

  pnga::ui::qt::BlockInspector widget;
  widget.setView(view);
  auto* table = widget.findChild<QTableWidget*>(
      QStringLiteral("blockInspectorTable"));
  QVERIFY(table != nullptr);
  QCOMPARE(table->rowCount(), 1);
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("stored"));
  QCOMPARE(table->item(0, 6)->text(), QStringLiteral("file[100..103)"));

  QSignalSpy hex_spy(&widget, &pnga::ui::qt::BlockInspector::showInHexRequested);
  QSignalSpy deflate_spy(
      &widget, &pnga::ui::qt::BlockInspector::showInDeflateRequested);
  auto* hex = widget.findChild<QPushButton*>();
  QVERIFY(hex != nullptr);
  hex->click();
  QCOMPARE(hex_spy.count(), 1);
  QCOMPARE(hex_spy.takeFirst().at(0).toULongLong(), qulonglong{100});
  // The second button is found by its stable label, avoiding child ordering.
  const auto buttons = widget.findChildren<QPushButton*>();
  QVERIFY(buttons.size() >= 2);
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in DEFLATE")) {
      button->click();
    }
  }
  QCOMPARE(deflate_spy.count(), 1);
  QCOMPARE(deflate_spy.takeFirst().at(0).toULongLong(), qulonglong{16});
}

QTEST_MAIN(BlockInspectorTest)
#include "block_inspector_test.moc"
