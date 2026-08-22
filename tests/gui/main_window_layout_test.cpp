// WP-5U2 MainWindow layout and Workspace settings tests. The test composes the
// app window but never opens a file, so no decoder or file I/O is involved.

#include "main_window.h"

#include <QtTest/QtTest>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>

class MainWindowLayoutTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void defaultLayoutHasRequiredRegions();
  void docksAreMovableFloatableAndClosable();
  void workspaceSettingsRoundTrip();
  void corruptSettingsFallBackToDefaults();
  void resetLayoutRestoresDefaultsWithoutFileState();
};

void MainWindowLayoutTest::init() {
  QSettings settings;
  settings.clear();
}

void MainWindowLayoutTest::defaultLayoutHasRequiredRegions() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();
  auto* splitter = window.findChild<QSplitter*>(QStringLiteral("previewHexSplitter"));
  QVERIFY(splitter != nullptr);
  QCOMPARE(splitter->count(), 2);
  QVERIFY(splitter->sizes().at(0) > splitter->sizes().at(1));

  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  QVERIFY(preview != nullptr);
  QCOMPARE(preview->count(), 5);
  QCOMPARE(preview->tabText(0), QStringLiteral("Image"));
  QCOMPARE(preview->tabText(1), QStringLiteral("Pixels"));
  QCOMPARE(preview->tabText(2), QStringLiteral("Filter Map"));
  QCOMPARE(preview->tabText(3), QStringLiteral("Filtered"));
  QCOMPARE(preview->tabText(4), QStringLiteral("Defiltered"));

  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(inspector != nullptr);
  QCOMPARE(inspector->count(), 5);
  QCOMPARE(inspector->tabText(0), QStringLiteral("Reconstruct"));
  QCOMPARE(inspector->tabText(1), QStringLiteral("Pixel"));
  QCOMPARE(inspector->tabText(2), QStringLiteral("Scanline"));
  QCOMPARE(inspector->tabText(3), QStringLiteral("Source"));
  QCOMPARE(inspector->tabText(4), QStringLiteral("Format Context"));

  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("chunksDock")) != nullptr);
  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("inspectorDock")) != nullptr);
  QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("xCoordinate")) != nullptr);
  QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("yCoordinate")) != nullptr);
  QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate")) != nullptr);
  QVERIFY(window.findChild<QComboBox*>(QStringLiteral("numericBase")) != nullptr);
  QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel")) != nullptr);
}

void MainWindowLayoutTest::docksAreMovableFloatableAndClosable() {
  MainWindow window;
  const auto check = [&window](const char* object_name) {
    auto* dock = window.findChild<QDockWidget*>(QString::fromLatin1(object_name));
    QVERIFY(dock != nullptr);
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
  };
  check("chunksDock");
  check("inspectorDock");
}

void MainWindowLayoutTest::workspaceSettingsRoundTrip() {
  {
    MainWindow window;
    auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
    auto* inspector =
        window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
    auto* base = window.findChild<QComboBox*>(QStringLiteral("numericBase"));
    auto* follow =
        window.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel"));
    QVERIFY(preview != nullptr);
    QVERIFY(inspector != nullptr);
    QVERIFY(base != nullptr);
    QVERIFY(follow != nullptr);
    preview->setCurrentIndex(3);
    inspector->setCurrentIndex(2);
    base->setCurrentIndex(1);
    follow->setChecked(false);
    QVERIFY(window.close());
  }

  MainWindow restored;
  QCOMPARE(restored.findChild<QTabWidget*>(QStringLiteral("previewTabs"))
               ->currentIndex(),
           3);
  QCOMPARE(restored.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"))
               ->currentIndex(),
           2);
  QCOMPARE(restored.findChild<QComboBox*>(QStringLiteral("numericBase"))
               ->currentIndex(),
           1);
  QVERIFY(!restored.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel"))
               ->isChecked());
}

void MainWindowLayoutTest::corruptSettingsFallBackToDefaults() {
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 999);
  settings.setValue(QStringLiteral("workspace/geometry"), QByteArray("bad"));
  settings.setValue(QStringLiteral("workspace/mainState"), QByteArray("bad"));
  settings.setValue(QStringLiteral("workspace/splitterState"), QByteArray("bad"));
  settings.setValue(QStringLiteral("view/numericBase"), 99);

  MainWindow window;
  QCOMPARE(window.findChild<QTabWidget*>(QStringLiteral("previewTabs"))
               ->currentIndex(),
           0);
  QCOMPARE(window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"))
               ->currentIndex(),
           0);
  QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("numericBase"))
               ->currentIndex(),
           0);
  QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel"))
              ->isChecked());
}

void MainWindowLayoutTest::resetLayoutRestoresDefaultsWithoutFileState() {
  MainWindow window;
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(preview != nullptr);
  QVERIFY(inspector != nullptr);
  preview->setCurrentIndex(4);
  inspector->setCurrentIndex(4);

  QAction* reset = nullptr;
  for (QAction* action : window.menuBar()->actions()) {
    if (action->menu() != nullptr &&
        action->menu()->title() == QStringLiteral("&View")) {
      for (QAction* child : action->menu()->actions()) {
        if (child->text() == QStringLiteral("&Reset Layout")) {
          reset = child;
        }
      }
    }
  }
  QVERIFY(reset != nullptr);
  reset->trigger();
  QCOMPARE(preview->currentIndex(), 0);
  QCOMPARE(inspector->currentIndex(), 0);
}

QTEST_MAIN(MainWindowLayoutTest)
#include "main_window_layout_test.moc"
