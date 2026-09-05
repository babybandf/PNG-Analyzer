// WP-5U15 Task 3: the extracted builder must create the same fully-parented
// widget graph with stable object identities and initial action state.

#include "main_window_ui.h"

#include <pnga/ui/qt/statistics_inspector.h>

#include <QtTest/QtTest>

class MainWindowUiTest : public QObject {
  Q_OBJECT
 private slots:
  void builderCreatesStableWidgetAndActionIdentities();
  void statisticsIsThirdTopLevelInspectorGroup();
};

void MainWindowUiTest::builderCreatesStableWidgetAndActionIdentities() {
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  QCOMPARE(window.centralWidget(), widgets.center_splitter);
  QCOMPARE(widgets.preview_tabs->objectName(), QStringLiteral("previewTabs"));
  QCOMPARE(widgets.preview_tabs->count(), 4);
  QCOMPARE(widgets.chunks_dock->objectName(), QStringLiteral("chunksDock"));
  QCOMPARE(widgets.inspector_dock->objectName(), QStringLiteral("inspectorDock"));
  QCOMPARE(widgets.close_action->objectName(), QStringLiteral("closeImageAction"));
  QVERIFY(!widgets.close_action->isEnabled());
  QCOMPARE(widgets.pixel_label->objectName(), QStringLiteral("pixelStatus"));
  QCOMPARE(widgets.validation_label->objectName(), QStringLiteral("validationStatus"));
}

void MainWindowUiTest::statisticsIsThirdTopLevelInspectorGroup() {
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  // WP-602G (R11): the top-level Inspector order is Reconstruction,
  // Compression, Statistics, and the Statistics widget pointer is exposed.
  QVERIFY(widgets.statistics_inspector != nullptr);
  QCOMPARE(widgets.statistics_inspector->objectName(),
           QStringLiteral("statisticsInspector"));
  QCOMPARE(widgets.inspector_tabs->count(), 3);
  QCOMPARE(widgets.inspector_tabs->tabText(0), QStringLiteral("Reconstruction"));
  QCOMPARE(widgets.inspector_tabs->tabText(1), QStringLiteral("Compression"));
  QCOMPARE(widgets.inspector_tabs->tabText(2), QStringLiteral("Statistics"));
  // The Reconstruction default is unchanged.
  QCOMPARE(widgets.inspector_tabs->currentIndex(), 0);
}

QTEST_MAIN(MainWindowUiTest)
#include "main_window_ui_test.moc"
