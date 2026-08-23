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
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTreeView>

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
  void coordinateToolbarScrollsLocallyWhenInspectorIsNarrow();
  void inspectorSwitchesKeepColumnWidths();
  void dockSeparatorsShowThreeDotAffordance();
  void chunkDockStaysResizableAndRedockableAfterOpen();
  void recentFilesMenuPersistsAndCapsHistory();
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
  QCOMPARE(inspector->count(), 3);
  QCOMPARE(inspector->tabText(0), QStringLiteral("Image"));
  QCOMPARE(inspector->tabText(1), QStringLiteral("Scanline"));
  QCOMPARE(inspector->tabText(2), QStringLiteral("Compression"));
  auto* image_pages = window.findChild<QTabWidget*>(QStringLiteral("imageInspectorPages"));
  auto* scanline_pages = window.findChild<QTabWidget*>(QStringLiteral("scanlineInspectorPages"));
  auto* compression_pages = window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  QVERIFY(image_pages != nullptr);
  QVERIFY(scanline_pages != nullptr);
  QVERIFY(compression_pages != nullptr);
  QCOMPARE(image_pages->tabText(0), QStringLiteral("Reconstruction"));
  QCOMPARE(image_pages->tabText(1), QStringLiteral("Pixel"));
  QCOMPARE(image_pages->tabText(2), QStringLiteral("Format Context"));
  QCOMPARE(scanline_pages->tabText(0), QStringLiteral("Scanline"));
  QCOMPARE(scanline_pages->tabText(1), QStringLiteral("Source"));
  QCOMPARE(compression_pages->tabText(0), QStringLiteral("DEFLATE Blocks"));
  QCOMPARE(compression_pages->tabText(1), QStringLiteral("Huffman"));
  QCOMPARE(compression_pages->tabText(2), QStringLiteral("Decode Trace"));

  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("chunksDock")) != nullptr);
  QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("inspectorDock")) != nullptr);
  QCOMPARE(window.corner(Qt::TopRightCorner), Qt::RightDockWidgetArea);
  QCOMPARE(window.corner(Qt::BottomRightCorner), Qt::RightDockWidgetArea);
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
  QVERIFY(window.isDockNestingEnabled());
  QVERIFY(window.dockOptions().testFlag(QMainWindow::GroupedDragging));
  const auto check = [&window](const char* object_name) {
    auto* dock = window.findChild<QDockWidget*>(QString::fromLatin1(object_name));
    QVERIFY(dock != nullptr);
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    QVERIFY(dock->allowedAreas().testFlag(Qt::RightDockWidgetArea));
  };
  check("chunksDock");
  check("inspectorDock");

  auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(inspector != nullptr);
  inspector->setFloating(true);
  QCoreApplication::processEvents();
  QVERIFY(inspector->isFloating());
  window.addDockWidget(Qt::RightDockWidgetArea, inspector);
  inspector->setFloating(false);
  QCoreApplication::processEvents();
  QCOMPARE(window.dockWidgetArea(inspector), Qt::RightDockWidgetArea);
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
    inspector->setCurrentIndex(1);
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
           1);
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
  window.show();
  QCoreApplication::processEvents();
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* chunks = window.findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector_dock =
      window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(preview != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(chunks != nullptr);
  QVERIFY(inspector_dock != nullptr);
  preview->setCurrentIndex(4);
  inspector->setCurrentIndex(4);
  chunks->setFloating(true);
  inspector_dock->setFloating(true);
  QCoreApplication::processEvents();
  QVERIFY(chunks->isFloating());
  QVERIFY(inspector_dock->isFloating());

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
  QCoreApplication::processEvents();
  QCOMPARE(preview->currentIndex(), 0);
  QCOMPARE(inspector->currentIndex(), 0);
  QVERIFY(!chunks->isFloating());
  QVERIFY(!inspector_dock->isFloating());
  QCOMPARE(window.dockWidgetArea(chunks), Qt::LeftDockWidgetArea);
  QCOMPARE(window.dockWidgetArea(inspector_dock), Qt::RightDockWidgetArea);
  QVERIFY(chunks->isVisible());
  QVERIFY(inspector_dock->isVisible());
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

void MainWindowLayoutTest::coordinateToolbarScrollsLocallyWhenInspectorIsNarrow() {
  MainWindow window;
  window.resize(900, 600);
  window.show();
  QCoreApplication::processEvents();
  auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  auto* toolbar = window.findChild<QWidget*>(QStringLiteral("coordinateToolbar"));
  auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("coordinateToolbarScroll"));
  QVERIFY(inspector != nullptr);
  QVERIFY(toolbar != nullptr);
  QVERIFY(scroll != nullptr);
  QCOMPARE(scroll->widget(), toolbar);
  QCOMPARE(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QVERIFY(inspector->minimumWidth() <= 300);
  QVERIFY(toolbar->width() >= scroll->viewport()->width());
}

void MainWindowLayoutTest::inspectorSwitchesKeepColumnWidths() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();
  auto* groups = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* image = window.findChild<QTabWidget*>(QStringLiteral("imageInspectorPages"));
  auto* scanline = window.findChild<QTabWidget*>(QStringLiteral("scanlineInspectorPages"));
  auto* compression = window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  auto* chunks = window.findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(groups != nullptr);
  QVERIFY(image != nullptr);
  QVERIFY(scanline != nullptr);
  QVERIFY(compression != nullptr);
  QVERIFY(chunks != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(chunks->width() >= chunks->minimumWidth());
  QVERIFY(inspector->width() >= inspector->minimumWidth());
  for (auto* splitter : window.findChildren<QSplitter*>()) {
    QCOMPARE(splitter->handleWidth(), 8);
  }
  const int chunk_width = chunks->width();
  const int inspector_width = inspector->width();
  for (int i = 0; i < 10; ++i) {
    groups->setCurrentIndex(i % groups->count());
    image->setCurrentIndex(i % image->count());
    scanline->setCurrentIndex(i % scanline->count());
    compression->setCurrentIndex(i % compression->count());
    QCoreApplication::processEvents();
    QCOMPARE(chunks->width(), chunk_width);
    QCOMPARE(inspector->width(), inspector_width);
  }
}

void MainWindowLayoutTest::dockSeparatorsShowThreeDotAffordance() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();
  const QImage screenshot = window.grab().toImage();
  const int extent = window.style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent,
                                                 nullptr, &window);
  QVERIFY(extent > 0);

  const auto checkDots = [&](QDockWidget* dock, Qt::DockWidgetArea area) {
    QVERIFY(dock != nullptr);
    const QRect dock_rect = dock->geometry();
    const int separator_x = area == Qt::LeftDockWidgetArea
                                ? dock_rect.right() + 1
                                : dock_rect.left() - extent;
    const QRect handle(separator_x, dock_rect.top(), extent, dock_rect.height());
    const int center_x = handle.center().x();
    const int center_y = handle.center().y();
    for (const int offset : {-4, 0, 4}) {
      const QColor dot = screenshot.pixelColor(center_x, center_y + offset);
      const QColor background =
          screenshot.pixelColor(center_x + 2, center_y + offset);
      QVERIFY(dot.red() + 5 < background.red());
      QVERIFY(dot.green() + 5 < background.green());
      QVERIFY(dot.blue() + 5 < background.blue());
    }
  };

  checkDots(window.findChild<QDockWidget*>(QStringLiteral("chunksDock")),
            Qt::LeftDockWidgetArea);
  checkDots(window.findChild<QDockWidget*>(QStringLiteral("inspectorDock")),
            Qt::RightDockWidgetArea);
}

void MainWindowLayoutTest::chunkDockStaysResizableAndRedockableAfterOpen() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  auto* chunks = window.findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* tree = window.findChild<QTreeView*>();
  QVERIFY(chunks != nullptr);
  QVERIFY(tree != nullptr);
  QVERIFY(window.styleSheet().contains(QStringLiteral("width: 8px")));
  QVERIFY(window.style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent,
                                      nullptr, &window) >= 8);
  QCOMPARE(tree->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  QCOMPARE(tree->header()->sectionResizeMode(0), QHeaderView::Interactive);

  window.resizeDocks({chunks}, {190}, Qt::Horizontal);
  QCoreApplication::processEvents();
  const int adjusted_width = chunks->width();
  QVERIFY(adjusted_width >= chunks->minimumWidth());

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();
  QCOMPARE(chunks->width(), adjusted_width);

  chunks->setFloating(true);
  QCoreApplication::processEvents();
  QVERIFY(chunks->isFloating());
  window.addDockWidget(Qt::LeftDockWidgetArea, chunks);
  chunks->setFloating(false);
  QCoreApplication::processEvents();
  QVERIFY(!chunks->isFloating());
}

void MainWindowLayoutTest::recentFilesMenuPersistsAndCapsHistory() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QStringList paths;
  for (int i = 0; i < 11; ++i) {
    const QString path = directory.filePath(
        QStringLiteral("image-%1.png").arg(i, 2, 10, QLatin1Char('0')));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("not a png") > 0);
    paths.push_back(path);
  }

  MainWindow window;
  auto* file_menu = window.menuBar()->actions().at(0)->menu();
  QVERIFY(file_menu != nullptr);
  auto* recent = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
  QVERIFY(recent != nullptr);
  const auto file_actions = file_menu->actions();
  QVERIFY(file_actions.size() >= 3);
  QCOMPARE(file_actions.at(0)->text(), QStringLiteral("&Open..."));
  QVERIFY(file_actions.at(1)->isSeparator());
  QCOMPARE(file_actions.at(2)->menu(), recent);

  for (const QString& path : paths) {
    QVERIFY(window.openFile(path));
    QCoreApplication::processEvents();
    QTest::qWait(5);
  }

  QSettings settings;
  const QStringList stored =
      settings.value(QStringLiteral("file/recentFiles")).toStringList();
  QCOMPARE(stored.size(), 10);
  QCOMPARE(stored.front(), QFileInfo(paths.back()).absoluteFilePath());
  QVERIFY(!stored.contains(QFileInfo(paths.front()).absoluteFilePath()));
  QCOMPARE(settings.value(QStringLiteral("file/lastOpenDirectory")).toString(),
           directory.path());
  QCOMPARE(recent->actions().size(), 10);
  QCOMPARE(recent->actions().front()->data().toString(), stored.front());

  MainWindow restored;
  auto* restored_recent =
      restored.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
  QVERIFY(restored_recent != nullptr);
  QCOMPARE(restored_recent->actions().size(), 10);
  QCOMPARE(restored_recent->actions().front()->data().toString(), stored.front());
}

QTEST_MAIN(MainWindowLayoutTest)
#include "main_window_layout_test.moc"
