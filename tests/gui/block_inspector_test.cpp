#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/ui/qt/block_inspector.h>

#include <QtTest/QtTest>

#include <QLabel>
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
  // Current | # | Type | Final | Input bits | Output bytes
  QCOMPARE(table->item(0, 0)->text(), QStringLiteral("●"));
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("2"));
  QCOMPARE(table->item(0, 2)->text(), QStringLiteral("stored"));
  QCOMPARE(table->item(0, 3)->text(), QStringLiteral("no"));
  QCOMPARE(table->item(0, 4)->text(), QStringLiteral("16..32"));
  QCOMPARE(table->item(0, 5)->text(), QStringLiteral("8..20"));

  // The Current row drives the details (no manual selection yet).
  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #2")));
  bool found_span = false;
  const auto labels = widget.findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->text() == QStringLiteral("file[100..103)")) {
      found_span = true;
      break;
    }
  }
  QVERIFY(found_span);

  QSignalSpy hex_spy(&widget, &pnga::ui::qt::BlockInspector::showInHexRequested);
  QSignalSpy deflate_spy(
      &widget, &pnga::ui::qt::BlockInspector::showInDeflateRequested);
  const auto buttons = widget.findChildren<QPushButton*>();
  QVERIFY(buttons.size() >= 2);
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      button->click();
    }
  }
  QCOMPARE(hex_spy.count(), 1);
  QCOMPARE(hex_spy.takeFirst().at(0).toULongLong(), qulonglong{100});
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
