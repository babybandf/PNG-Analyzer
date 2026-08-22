// WP-5U2 MainWindow layout and Workspace settings tests. The test composes the
// app window but never opens a file, so no decoder or file I/O is involved.

#include "main_window.h"

#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/selection_bus.h>

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
  void coordinateInteractionUsesToolbarAndKeyboard();
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
  QCOMPARE(inspector->count(), 7);
  QCOMPARE(inspector->tabText(0), QStringLiteral("Reconstruct"));
  QCOMPARE(inspector->tabText(1), QStringLiteral("Pixel"));
  QCOMPARE(inspector->tabText(2), QStringLiteral("Scanline"));
  QCOMPARE(inspector->tabText(3), QStringLiteral("Source"));
  QCOMPARE(inspector->tabText(4), QStringLiteral("Format Context"));
  QCOMPARE(inspector->tabText(5), QStringLiteral("DEFLATE / Block"));
  QCOMPARE(inspector->tabText(6), QStringLiteral("DEFLATE / Huffman Tables"));

  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("chunksDock")) != nullptr);
  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("inspectorDock")) != nullptr);
  QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("xCoordinate")) != nullptr);
  QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("yCoordinate")) != nullptr);
  QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate")) != nullptr);
  QVERIFY(window.findChild<QComboBox*>(QStringLiteral("numericBase")) != nullptr);
  auto* hex_source =
      window.findChild<QComboBox*>(QStringLiteral("hexSource"));
  QVERIFY(hex_source != nullptr);
  QCOMPARE(hex_source->count(), 4);
  QCOMPARE(hex_source->itemText(0), QStringLiteral("File"));
  QCOMPARE(hex_source->itemText(1), QStringLiteral("IDAT Stream"));
  QCOMPARE(hex_source->itemText(2), QStringLiteral("Inflated"));
  QCOMPARE(hex_source->itemText(3), QStringLiteral("Defiltered"));
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
    auto* source = window.findChild<QComboBox*>(QStringLiteral("hexSource"));
    auto* follow =
        window.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel"));
    QVERIFY(preview != nullptr);
    QVERIFY(inspector != nullptr);
    QVERIFY(base != nullptr);
    QVERIFY(source != nullptr);
    QVERIFY(follow != nullptr);
    preview->setCurrentIndex(3);
    inspector->setCurrentIndex(2);
    base->setCurrentIndex(1);
    source->setCurrentIndex(1);
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
  QCOMPARE(restored.findChild<QComboBox*>(QStringLiteral("hexSource"))
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

void MainWindowLayoutTest::coordinateInteractionUsesToolbarAndKeyboard() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  auto* bus = window.findChild<pnga::ui::qt::SelectionBus*>();
  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(image != nullptr);
  QVERIFY(bus != nullptr);
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);

  QImage delivered(2, 2, QImage::Format_RGBA8888);
  delivered.fill(qRgba(1, 2, 3, 255));
  image->setImage(delivered);
  QCoreApplication::processEvents();
  QTest::mouseClick(image, Qt::LeftButton, Qt::NoModifier,
                    image->rect().center());
  QVERIFY(lock->isChecked());
  QVERIFY(x->value() >= 0 && x->value() < 2);
  QVERIFY(y->value() >= 0 && y->value() < 2);
  QVERIFY(bus->current().image.has_value());
  QCOMPARE(bus->current().image->x, static_cast<std::uint64_t>(x->value()));
  QCOMPARE(bus->current().image->y, static_cast<std::uint64_t>(y->value()));

  const int old_x = x->value();
  const int old_y = y->value();
  QTest::keyClick(image, old_x == 0 ? Qt::Key_Right : Qt::Key_Left);
  QCOMPARE(x->value(), old_x == 0 ? 1 : 0);
  QCOMPARE(y->value(), old_y);
  QTest::keyClick(image, Qt::Key_Escape);
  QVERIFY(!lock->isChecked());
  QVERIFY(!bus->current().image.has_value());
}

QTEST_MAIN(MainWindowLayoutTest)
#include "main_window_layout_test.moc"
