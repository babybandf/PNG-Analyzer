// WP-5U2 MainWindow layout and Workspace settings tests. The test composes the
// app window but never opens a file, so no decoder or file I/O is involved.

#include "main_window.h"

#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
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
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
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
  void workspaceV1MigratesStablePages();
  void corruptSettingsFallBackToDefaults();
  void resetLayoutRestoresDefaultsWithoutFileState();
  void coordinateInteractionUsesToolbarAndKeyboard();
  void coordinateToolbarScrollsLocallyWhenInspectorIsNarrow();
  void inspectorSwitchesKeepColumnWidths();
  void dockSeparatorsShowThreeDotAffordance();
  void chunkDockStaysResizableAndRedockableAfterOpen();
  void openingFileResetsPrimaryViewsAndStoresLastTarget();
  void closeImageClearsDocumentAndDisablesAction();
  void hexHighlightsSelectedChunkAfterStageCompletes();
  void recentFilesMenuPersistsAndCapsHistory();
  void viewMenuTogglesCoreViewsAndFileHasExit();
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
  QCOMPARE(preview->count(), 4);
  QCOMPARE(preview->tabText(0), QStringLiteral("Image"));
  QCOMPARE(preview->tabText(1), QStringLiteral("Pixels"));
  QCOMPARE(preview->tabText(2), QStringLiteral("Filtered"));
  QCOMPARE(preview->tabText(3), QStringLiteral("Unfiltered"));

  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(inspector != nullptr);
  QCOMPARE(inspector->count(), 2);
  QCOMPARE(inspector->tabText(0), QStringLiteral("Reconstruction"));
  QCOMPARE(inspector->tabText(1), QStringLiteral("Compression"));
  auto* compression_pages = window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  QVERIFY(compression_pages != nullptr);
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
  auto* base = window.findChild<QPushButton*>(QStringLiteral("numericBase"));
  QVERIFY(base != nullptr);
  QVERIFY(base->isFlat());
  QCOMPARE(base->text(), QStringLiteral("HEX"));
  base->click();
  QCOMPARE(base->text(), QStringLiteral("DEC"));
  base->click();
  QCOMPARE(base->text(), QStringLiteral("HEX"));
  auto* hex_source =
      window.findChild<pnga::ui::qt::HexSourceTabBar*>(
          QStringLiteral("hexSourceTabs"));
  QVERIFY(hex_source != nullptr);
  QCOMPARE(hex_source->count(), 4);
  QCOMPARE(hex_source->tabText(0), QStringLiteral("File"));
  QCOMPARE(hex_source->tabText(1), QStringLiteral("IDAT"));
  QCOMPARE(hex_source->tabText(2), QStringLiteral("Inflated"));
  QCOMPARE(hex_source->tabText(3), QStringLiteral("Unfiltered"));
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
    auto* base = window.findChild<QPushButton*>(QStringLiteral("numericBase"));
    auto* source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
        QStringLiteral("hexSourceTabs"));
    QVERIFY(preview != nullptr);
    QVERIFY(inspector != nullptr);
    QVERIFY(base != nullptr);
    QVERIFY(source != nullptr);
    preview->setCurrentIndex(2);
    inspector->setCurrentIndex(1);
    base->click();
    source->setCurrentIndex(1);
    QVERIFY(window.close());
  }

  MainWindow restored;
  QCOMPARE(restored.findChild<QTabWidget*>(QStringLiteral("previewTabs"))
               ->currentIndex(),
           2);
  QCOMPARE(restored.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"))
               ->currentIndex(),
           1);
  QCOMPARE(restored.findChild<QPushButton*>(QStringLiteral("numericBase"))
               ->text(),
           QStringLiteral("DEC"));
  QCOMPARE(restored.findChild<pnga::ui::qt::HexSourceTabBar*>(
               QStringLiteral("hexSourceTabs"))
               ->currentIndex(),
           1);
}

void MainWindowLayoutTest::workspaceV1MigratesStablePages() {
  {
    MainWindow seed;
    QVERIFY(seed.close());
  }
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 1);
  settings.setValue(QStringLiteral("workspace/previewTab"), 3);
  settings.setValue(QStringLiteral("workspace/inspectorTab"), 2);
  settings.setValue(QStringLiteral("workspace/imagePage"), 2);
  settings.setValue(QStringLiteral("workspace/scanlinePage"), 1);
  settings.setValue(QStringLiteral("workspace/compressionPage"), 1);

  MainWindow window;
  QCOMPARE(window.findChild<QTabWidget*>(QStringLiteral("previewTabs"))
               ->currentIndex(),
           2);
  QCOMPARE(window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"))
               ->currentIndex(),
           1);
  QCOMPARE(window.findChild<QTabWidget*>(
               QStringLiteral("compressionInspectorPages"))
               ->currentIndex(),
           1);
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
  QCOMPARE(window.findChild<QPushButton*>(QStringLiteral("numericBase"))
               ->text(),
           QStringLiteral("HEX"));
  QCOMPARE(window.findChild<pnga::ui::qt::HexSourceTabBar*>(
               QStringLiteral("hexSourceTabs"))
               ->currentIndex(),
           0);
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
  preview->setCurrentIndex(3);
  inspector->setCurrentIndex(1);
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
  auto* status = window.findChild<QLabel*>(QStringLiteral("pixelStatus"));
  QVERIFY(image != nullptr);
  QVERIFY(bus != nullptr);
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  QVERIFY(status != nullptr);

  QImage delivered(2, 2, QImage::Format_RGBA8888);
  delivered.fill(qRgba(1, 2, 3, 255));
  image->setImage(delivered);
  QCoreApplication::processEvents();
  const auto status_for = [&image](const QPoint& point) {
    const auto rgba = image->rgbaAt(point.x(), point.y());
    return QStringLiteral("pixel (%1, %2) RGBA(%3, %4, %5, %6)")
        .arg(point.x())
        .arg(point.y())
        .arg((*rgba)[0])
        .arg((*rgba)[1])
        .arg((*rgba)[2])
        .arg((*rgba)[3]);
  };
  const QPoint hover_point = image->rect().center();
  const auto hovered_pixel = image->imagePixelAt(hover_point);
  QVERIFY(hovered_pixel.has_value());
  QTest::mouseMove(image, hover_point);
  QCOMPARE(status->text(), status_for(*hovered_pixel));
  QTest::mouseClick(image, Qt::LeftButton, Qt::NoModifier,
                    hover_point);
  QVERIFY(lock->isChecked());
  QVERIFY(x->value() >= 0 && x->value() < 2);
  QVERIFY(y->value() >= 0 && y->value() < 2);
  QVERIFY(bus->current().image.has_value());
  QCOMPARE(bus->current().image->x, static_cast<std::uint64_t>(x->value()));
  QCOMPARE(bus->current().image->y, static_cast<std::uint64_t>(y->value()));

  QPoint outside_point;
  bool found_outside = false;
  for (int row = 0; row < image->height() && !found_outside; ++row) {
    for (int column = 0; column < image->width(); ++column) {
      const QPoint candidate(column, row);
      if (!image->imagePixelAt(candidate).has_value()) {
        outside_point = candidate;
        found_outside = true;
        break;
      }
    }
  }
  QVERIFY(found_outside);
  QTest::mouseMove(image, outside_point);
  QCOMPARE(status->text(), status_for(QPoint(x->value(), y->value())));
  QVERIFY(QMetaObject::invokeMethod(image, "pixelHoverLeft",
                                    Qt::DirectConnection));

  const int old_x = x->value();
  const int old_y = y->value();
  QTest::keyClick(image, old_x == 0 ? Qt::Key_Right : Qt::Key_Left);
  QCOMPARE(x->value(), old_x == 0 ? 1 : 0);
  QCOMPARE(y->value(), old_y);
  QTest::keyClick(image, Qt::Key_Escape);
  QVERIFY(!lock->isChecked());
  QVERIFY(!bus->current().image.has_value());
  QCOMPARE(status->text(), QStringLiteral("No image"));
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
  QVERIFY(toolbar->width() > 0);
  QVERIFY(scroll->viewport()->width() > 0);
}

void MainWindowLayoutTest::inspectorSwitchesKeepColumnWidths() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();
  auto* groups = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* compression = window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  auto* chunks = window.findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(groups != nullptr);
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
  auto* chunk_splitter =
      window.findChild<QSplitter*>(QStringLiteral("chunksDetailSplitter"));
  auto* detail_table =
      window.findChild<QTableWidget*>(QStringLiteral("chunkDetailTable"));
  auto* detail_summary =
      window.findChild<QLabel*>(QStringLiteral("chunkDetailSummary"));
  auto* detail_description =
      window.findChild<QLabel*>(QStringLiteral("chunkDetailDescription"));
  QVERIFY(chunks != nullptr);
  QVERIFY(tree != nullptr);
  QVERIFY(chunk_splitter != nullptr);
  QVERIFY(detail_table != nullptr);
  QVERIFY(detail_summary != nullptr);
  QVERIFY(detail_description != nullptr);
  QCOMPARE(detail_description->textFormat(), Qt::RichText);
  QCOMPARE(chunk_splitter->orientation(), Qt::Vertical);
  QCOMPARE(detail_table->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QCOMPARE(detail_table->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QVERIFY(chunk_splitter->sizes().at(0) > 0);
  QVERIFY(chunk_splitter->sizes().at(1) > 0);
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
  QVERIFY(window.windowTitle().contains(
      QStringLiteral(" — %1").arg(QFileInfo(png.fileName()).absoluteFilePath())));
  QVERIFY(window.findChild<QLabel*>(QStringLiteral("filePathStatus")) == nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(detail_summary->text().contains(QStringLiteral("IHDR")),
                           1000);
  QTRY_VERIFY_WITH_TIMEOUT(
      detail_description->text().contains(QStringLiteral("<b>IHDR</b>")),
      1000);
  QVERIFY(detail_description->text().contains(QStringLiteral("image header")));
  QVERIFY(detail_table->rowCount() >= 7);
  chunk_splitter->setSizes({120, 260});
  QCoreApplication::processEvents();
  QVERIFY(chunk_splitter->sizes().at(0) >= 80);
  QVERIFY(chunk_splitter->sizes().at(1) >= 80);
  tree->selectionModel()->setCurrentIndex(
      tree->model()->index(1, 0),
      QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  QTRY_VERIFY_WITH_TIMEOUT(detail_summary->text().contains(QStringLiteral("IDAT")),
                           1000);
  QTRY_VERIFY_WITH_TIMEOUT(
      detail_description->text().contains(QStringLiteral("<b>IDAT</b>")),
      1000);
  QVERIFY(detail_description->text().contains(QStringLiteral("compressed image data")));
  QVERIFY(detail_table->rowCount() >= 4);
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
  QVERIFY(file_actions.size() >= 4);
  QCOMPARE(file_actions.at(0)->text(), QStringLiteral("&Open..."));
  QCOMPARE(file_actions.at(1)->objectName(),
           QStringLiteral("closeImageAction"));
  QVERIFY(file_actions.at(2)->isSeparator());
  QCOMPARE(file_actions.at(3)->menu(), recent);

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
  restored_recent->actions().front()->trigger();
  QCoreApplication::processEvents();
  QVERIFY(restored.windowTitle().contains(
      QFileInfo(paths.back()).absoluteFilePath()));
}

void MainWindowLayoutTest::openingFileResetsPrimaryViewsAndStoresLastTarget() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("selected.png"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QVERIFY(file.write("not a png") > 0);
  file.close();

  MainWindow window;
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(preview != nullptr);
  QVERIFY(inspector != nullptr);
  preview->setCurrentIndex(2);
  inspector->setCurrentIndex(1);
  QVERIFY(window.openFile(path));
  QCOMPARE(preview->currentIndex(), 0);
  QCOMPARE(inspector->currentIndex(), 0);

  QSettings settings;
  QCOMPARE(settings.value(QStringLiteral("file/lastOpenFile")).toString(),
           QFileInfo(path).absoluteFilePath());
}

void MainWindowLayoutTest::closeImageClearsDocumentAndDisablesAction() {
  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();

  MainWindow window;
  auto* close_action = window.findChild<QAction*>(
      QStringLiteral("closeImageAction"));
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  auto* tree = window.findChild<QTreeView*>();
  QVERIFY(close_action != nullptr);
  QVERIFY(image != nullptr);
  QVERIFY(tree != nullptr);
  QVERIFY(!close_action->isEnabled());

  QVERIFY(window.openFile(png.fileName()));
  QVERIFY(close_action->isEnabled());
  QVERIFY(window.windowTitle().contains(QFileInfo(png.fileName()).absoluteFilePath()));
  QVERIFY(tree->model()->rowCount() > 0);

  close_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(!close_action->isEnabled());
  QVERIFY(image->image().isNull());
  QCOMPARE(tree->model()->rowCount(), 0);
  QVERIFY(!window.windowTitle().contains(
      QFileInfo(png.fileName()).absoluteFilePath()));
  auto* status = window.findChild<QLabel*>(QStringLiteral("pixelStatus"));
  QVERIFY(status != nullptr);
  QCOMPARE(status->text(), QStringLiteral("No image"));
}

void MainWindowLayoutTest::hexHighlightsSelectedChunkAfterStageCompletes() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* hex =
      window.findChild<pnga::ui::qt::HexView*>(QStringLiteral("hexView"));
  QVERIFY(hex != nullptr);
  // The default IHDR chunk is selected and highlighted synchronously on open.
  QCOMPARE(hex->highlightCount(), std::size_t{3});
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), std::uint64_t{8});  // IHDR header offset

  // Wait until the async stage/query workers have completed. The Compression
  // trace context leaves its initial state only after onStageDone opens the
  // trace pipeline, so this guarantees onStageDone's hex-source refresh (which
  // previously cleared the highlight) has run.
  auto* context = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      !context->text().contains(QStringLiteral("Select and lock a pixel")) &&
          !context->text().contains(QStringLiteral("not indexed")) &&
          !context->text().contains(QStringLiteral("Open a PNG")),
      5000);
  QCOMPARE(hex->highlightCount(), std::size_t{3});
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), std::uint64_t{8});
}

void MainWindowLayoutTest::viewMenuTogglesCoreViewsAndFileHasExit() {
  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  auto* chunks =
      window.findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector =
      window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  auto* hex_panel = window.findChild<QWidget*>(QStringLiteral("hexPanel"));
  QVERIFY(chunks != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(hex_panel != nullptr);

  QMenu* file_menu = nullptr;
  QMenu* view_menu = nullptr;
  for (QAction* action : window.menuBar()->actions()) {
    if (action->menu() == nullptr) {
      continue;
    }
    if (action->menu()->title() == QStringLiteral("&File")) {
      file_menu = action->menu();
    } else if (action->menu()->title() == QStringLiteral("&View")) {
      view_menu = action->menu();
    }
  }
  QVERIFY(file_menu != nullptr);
  QVERIFY(view_menu != nullptr);

  const auto action_by_name = [](QMenu* menu, const QString& name) {
    for (QAction* action : menu->actions()) {
      if (action->objectName() == name) {
        return action;
      }
    }
    return static_cast<QAction*>(nullptr);
  };
  QVERIFY(action_by_name(file_menu, QStringLiteral("exitAction")) != nullptr);
  QAction* chunk_action =
      action_by_name(view_menu, QStringLiteral("showChunkList"));
  QAction* hex_action = action_by_name(view_menu, QStringLiteral("showHexView"));
  QAction* inspector_action =
      action_by_name(view_menu, QStringLiteral("showInspector"));
  QVERIFY(chunk_action != nullptr);
  QVERIFY(hex_action != nullptr);
  QVERIFY(inspector_action != nullptr);
  QVERIFY(chunk_action->isChecked());
  QVERIFY(hex_action->isChecked());
  QVERIFY(inspector_action->isChecked());

  chunk_action->trigger();
  hex_action->trigger();
  inspector_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(!chunks->isVisible());
  QVERIFY(!hex_panel->isVisible());
  QVERIFY(!inspector->isVisible());

  chunk_action->trigger();
  hex_action->trigger();
  inspector_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(chunks->isVisible());
  QVERIFY(hex_panel->isVisible());
  QVERIFY(inspector->isVisible());
}

QTEST_MAIN(MainWindowLayoutTest)
#include "main_window_layout_test.moc"
