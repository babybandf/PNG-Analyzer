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
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
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
#include <QUrl>

namespace {

// Drives Qt's internal dock drag machinery (QDockWidgetPrivate::startDrag →
// QMainWindowLayout::hover → endDrag) by delivering the mouse events of a
// title-bar drag that ends over `release_global`. Not equivalent to
// QDockWidget::setFloating(), which never enters the drag machinery.
void drag_dock_title_to(QDockWidget* dock, const QPoint& release_global) {
  const QPoint press_global =
      dock->mapToGlobal(QPoint(dock->rect().center().x(), 9));
  constexpr int kSteps = 12;
  const auto send_mouse = [&](QEvent::Type type, const QPoint& global_pos,
                              Qt::MouseButtons buttons) {
    QMouseEvent event(type,
                      QPointF(dock->mapFromGlobal(global_pos)),
                      QPointF(global_pos), Qt::LeftButton, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(dock, &event);
    QCoreApplication::processEvents();
  };
  send_mouse(QEvent::MouseButtonPress, press_global, Qt::LeftButton);
  for (int step = 1; step <= kSteps; ++step) {
    send_mouse(QEvent::MouseMove,
               press_global + (release_global - press_global) * step / kSteps,
               Qt::LeftButton);
  }
  send_mouse(QEvent::MouseButtonRelease, release_global, Qt::NoButton);
  QCoreApplication::processEvents();
}

// A floating dock uses native window decorations (wmSupportsNativeWindowDeco
// is true offscreen, as on macOS), so a real title-bar double-click arrives
// as the non-client-area event that QDockWidget::event routes to
// toggleTopLevel(); the client-area QTest::mouseDClick is ignored there.
void send_dock_title_dblclick(QDockWidget* dock) {
  const QPoint global =
      dock->mapToGlobal(QPoint(dock->rect().center().x(), 9));
  QMouseEvent event(QEvent::NonClientAreaMouseButtonDblClick,
                    QPointF(dock->mapFromGlobal(global)), QPointF(global),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(dock, &event);
  QCoreApplication::processEvents();
}

}  // namespace

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
  void resetLayoutRedocksDraggedInspectorInNormativeArea();
  void dockTitleDoubleClickRedocksDraggedInspector();
  void coordinateInteractionUsesToolbarAndKeyboard();
  void coordinateToolbarScrollsLocallyWhenInspectorIsNarrow();
  void inspectorSwitchesKeepColumnWidths();
  void dockSeparatorsShowThreeDotAffordance();
  void chunkDockStaysResizableAndRedockableAfterOpen();
  void openingFileResetsPrimaryViewsAndStoresLastTarget();
  void closeImageClearsDocumentAndDisablesAction();
  void dragAndDropOpensLocalPng();
  void hexHighlightsSelectedChunkAfterStageCompletes();
  void recentFilesMenuPersistsAndCapsHistory();
  void viewMenuTogglesCoreViewsAndFileHasExit();
  // WP-5U15 Task 1 characterization: facade identities and replacement reset.
  void facadeKeepsStableActionAndStatusIdentities();
  void replacingOpenDocumentResetsVisiblePrimaryState();
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

void MainWindowLayoutTest::resetLayoutRedocksDraggedInspectorInNormativeArea() {
  // Product-owner repro: the Inspector starts docked in its normative area
  // and is then dragged by its title bar with Qt's dock drag machinery
  // (not setFloating()) and released over the center of the image viewport.
  // Reset Layout must restore the normative topology regardless of that drag.
  const auto rect_text = [](const QRect& rect) {
    return QStringLiteral("(%1,%2 %3x%4)")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
  };
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();
  auto* inspector_dock =
      window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(inspector_dock != nullptr);
  QVERIFY(image != nullptr);
  QVERIFY(!inspector_dock->isFloating());
  QCOMPARE(window.dockWidgetArea(inspector_dock), Qt::RightDockWidgetArea);

  const QPoint release_global = image->mapToGlobal(image->rect().center());
  drag_dock_title_to(inspector_dock, release_global);

  // The drag machinery leaves the Inspector floating over the release point
  // (the reported pre-reset state; observed on offscreen and native runs).
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

  // Frozen layout contract (flow-ui §14/§20): after Reset Layout the
  // Inspector is docked again in the right dock area, visible, spanning the
  // right side of the window with the normative width bounds — never at the
  // dragged position.
  QVERIFY2(!inspector_dock->isFloating(),
           qPrintable(QStringLiteral("still floating at %1")
                          .arg(rect_text(inspector_dock->geometry()))));
  QCOMPARE(window.dockWidgetArea(inspector_dock), Qt::RightDockWidgetArea);
  QVERIFY(inspector_dock->isVisible());
  const QRect dock_geometry = inspector_dock->geometry();
  QVERIFY2(dock_geometry.center().x() > window.width() / 2,
           qPrintable(QStringLiteral("dock geometry %1 in window width %2")
                          .arg(rect_text(dock_geometry))
                          .arg(window.width())));
  QVERIFY2(inspector_dock->width() >= inspector_dock->minimumWidth(),
           qPrintable(QStringLiteral("dock width %1 < minimum %2")
                          .arg(inspector_dock->width())
                          .arg(inspector_dock->minimumWidth())));
  QVERIFY2(inspector_dock->height() >= window.height() / 2,
           qPrintable(QStringLiteral("dock height %1 vs window height %2")
                          .arg(inspector_dock->height())
                          .arg(window.height())));
}

void MainWindowLayoutTest::dockTitleDoubleClickRedocksDraggedInspector() {
  // Second symptom of the frozen drag state: after dragging the Inspector
  // out, double-clicking its floating title bar must re-dock it into its
  // previous normative dock area (Qt's title-bar double-click contract),
  // not embed it in place at the dragged position.
  const auto rect_text = [](const QRect& rect) {
    return QStringLiteral("(%1,%2 %3x%4)")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
  };
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();
  auto* inspector_dock =
      window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(inspector_dock != nullptr);
  QVERIFY(image != nullptr);
  QVERIFY(!inspector_dock->isFloating());
  QCOMPARE(window.dockWidgetArea(inspector_dock), Qt::RightDockWidgetArea);

  drag_dock_title_to(inspector_dock,
                     image->mapToGlobal(image->rect().center()));
  QVERIFY(inspector_dock->isFloating());

  send_dock_title_dblclick(inspector_dock);
  QCoreApplication::processEvents();

  // The title-bar double-click re-docks the Inspector into its previous
  // normative area with normative geometry — never in-place at the dragged
  // position.
  QVERIFY2(!inspector_dock->isFloating(),
           qPrintable(QStringLiteral("still floating at %1")
                          .arg(rect_text(inspector_dock->geometry()))));
  QCOMPARE(window.dockWidgetArea(inspector_dock), Qt::RightDockWidgetArea);
  QVERIFY(inspector_dock->isVisible());
  const QRect dock_geometry = inspector_dock->geometry();
  QVERIFY2(dock_geometry.center().x() > window.width() / 2,
           qPrintable(QStringLiteral("dock geometry %1 in window width %2")
                          .arg(rect_text(dock_geometry))
                          .arg(window.width())));
  QVERIFY2(inspector_dock->width() >= inspector_dock->minimumWidth(),
           qPrintable(QStringLiteral("dock width %1 < minimum %2")
                          .arg(inspector_dock->width())
                          .arg(inspector_dock->minimumWidth())));
  QVERIFY2(inspector_dock->height() >= window.height() / 2,
           qPrintable(QStringLiteral("dock height %1 vs window height %2")
                          .arg(inspector_dock->height())
                          .arg(window.height())));
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

void MainWindowLayoutTest::dragAndDropOpensLocalPng() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString png_path =
      directory.filePath(QStringLiteral("拖放文件.PNG"));
  QFile png(png_path);
  QVERIFY(png.open(QIODevice::WriteOnly));
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.close();

  MainWindow window;
  window.show();
  QCoreApplication::processEvents();

  QMimeData png_mime_data;
  png_mime_data.setUrls({QUrl::fromLocalFile(png_path)});
  QDragEnterEvent png_enter_event(QPoint(12, 12), Qt::CopyAction,
                                  &png_mime_data, Qt::LeftButton,
                                  Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &png_enter_event);
  QVERIFY(png_enter_event.isAccepted());

  QDropEvent png_drop_event(QPointF(12, 12), Qt::CopyAction,
                            &png_mime_data, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &png_drop_event);
  QVERIFY(png_drop_event.isAccepted());
  QVERIFY(window.windowTitle().contains(QFileInfo(png_path).absoluteFilePath()));

  const QString text_path = directory.filePath(QStringLiteral("other.txt"));
  QFile text_file(text_path);
  QVERIFY(text_file.open(QIODevice::WriteOnly));
  QVERIFY(text_file.write("not a PNG") > 0);
  text_file.close();
  QMimeData text_mime_data;
  text_mime_data.setUrls({QUrl::fromLocalFile(text_path)});
  QDragEnterEvent text_enter_event(QPoint(12, 12), Qt::CopyAction,
                                   &text_mime_data, Qt::LeftButton,
                                   Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &text_enter_event);
  QVERIFY(!text_enter_event.isAccepted());
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

void MainWindowLayoutTest::facadeKeepsStableActionAndStatusIdentities() {
  MainWindow window;
  QCOMPARE(window.windowTitle(), QStringLiteral("PNG Analyzer"));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("closeImageAction")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("exitAction")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showHexView")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showChunkList")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showInspector")));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("pixelStatus"))->text(),
           QStringLiteral("No image"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("validationStatus"))->text(),
           QStringLiteral("Validation: not loaded"));
}

void MainWindowLayoutTest::replacingOpenDocumentResetsVisiblePrimaryState() {
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QTemporaryFile first;
  QVERIFY(first.open());
  QCOMPARE(first.write(bytes), bytes.size());
  first.flush();
  QTemporaryFile second;
  QVERIFY(second.open());
  QCOMPARE(second.write(bytes), bytes.size());
  second.flush();

  MainWindow window;
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* inspector =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* close_action =
      window.findChild<QAction*>(QStringLiteral("closeImageAction"));
  QVERIFY(preview != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(close_action != nullptr);

  QVERIFY(window.openFile(first.fileName()));
  QCoreApplication::processEvents();
  preview->setCurrentIndex(1);
  inspector->setCurrentIndex(1);
  QVERIFY(close_action->isEnabled());

  QVERIFY(window.openFile(second.fileName()));
  QCoreApplication::processEvents();
  QCOMPARE(preview->currentIndex(), 0);
  QCOMPARE(inspector->currentIndex(), 0);
  QVERIFY(close_action->isEnabled());
  QVERIFY(window.windowTitle().contains(
      QStringLiteral(" — %1").arg(QFileInfo(second.fileName()).absoluteFilePath())));
}

QTEST_MAIN(MainWindowLayoutTest)
#include "main_window_layout_test.moc"
