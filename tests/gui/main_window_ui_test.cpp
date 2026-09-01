// WP-5U15 Task 3: the extracted builder must create the same fully-parented
// widget graph with stable object identities and initial action state.

#include "main_window_ui.h"

#include <QtTest/QtTest>

class MainWindowUiTest : public QObject {
  Q_OBJECT
 private slots:
  void builderCreatesStableWidgetAndActionIdentities();
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

QTEST_MAIN(MainWindowUiTest)
#include "main_window_ui_test.moc"
