#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/ui/qt/block_inspector.h>

#include <QtTest/QtTest>

#include <QLabel>
#include <QColor>
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
  row.physical_spans.push_back(
      {pnga::trace_model::ProvenanceSpace::kPhysicalFile, 200, 2, 0, 0,
       false});
  view.rows.push_back(row);

  pnga::ui::qt::BlockInspector widget;
  widget.setView(view);
  auto* table = widget.findChild<QTableWidget*>(
      QStringLiteral("blockInspectorTable"));
  QVERIFY(table != nullptr);
  QCOMPARE(table->rowCount(), 1);
  // # | Type | Final | Input bits | Output bytes
  QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2"));
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("stored"));
  QCOMPARE(table->item(0, 2)->text(), QStringLiteral("no"));
  QCOMPARE(table->item(0, 3)->text(), QStringLiteral("16..32"));
  QCOMPARE(table->item(0, 4)->text(), QStringLiteral("8..20"));
  QCOMPARE(table->item(0, 0)->background().color(),
           QColor(QStringLiteral("#FFF4CC")));

  // The Current row drives the details (no manual selection yet).
  auto* details_title =
      widget.findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
  QVERIFY(details_title != nullptr);
  QVERIFY(details_title->text().contains(QStringLiteral("Block #2")));
  bool found_span = false;
  const auto labels = widget.findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->text().contains(QStringLiteral("file[100..103)")) &&
        label->text().contains(QStringLiteral("file[200..202)"))) {
      found_span = true;
      break;
    }
  }
  QVERIFY(found_span);

  QSignalSpy hex_spy(&widget, &pnga::ui::qt::BlockInspector::showInHexRequested);
  QSignalSpy hex_ranges_spy(
      &widget, &pnga::ui::qt::BlockInspector::showInHexSpansRequested);
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
  QCOMPARE(hex_ranges_spy.count(), 1);
  const auto ranges =
      qvariant_cast<QVector<QPair<quint64, quint64>>>(
          hex_ranges_spy.takeFirst().at(0));
  QCOMPARE(ranges.size(), 2);
  QCOMPARE(ranges[0], qMakePair(quint64{100}, quint64{3}));
  QCOMPARE(ranges[1], qMakePair(quint64{200}, quint64{2}));
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
