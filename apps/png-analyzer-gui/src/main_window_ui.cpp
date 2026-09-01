// WP-5U15: MainWindow construction moved verbatim from the facade
// constructor. Every allocation, objectName, accessible name, shortcut,
// dock feature/area, minimum size, stretch factor, initial splitter size,
// tab order, menu/tab/status text and action state is preserved. The builder
// connects nothing; MainWindow performs all behavior wiring afterwards.

#include "main_window_ui.h"

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_detail_panel.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>
#include <pnga/ui/qt/trace_inspector_binding.h>

#include <QActionGroup>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenuBar>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

MainWindowWidgets buildMainWindowUi(
    QMainWindow& window, pnga::ui::qt::ApplicationTheme* theme) {
  MainWindowWidgets widgets;
  window.setWindowTitle(QStringLiteral("PNG Analyzer"));
  window.setAcceptDrops(true);
  // Standalone layout tests construct MainWindow without the application
  // controller. Keep their separator contract while the product build uses
  // the centralized stylesheet installed by ApplicationTheme.
  if (qApp == nullptr || qApp->styleSheet().isEmpty()) {
    window.setStyleSheet(QStringLiteral(
        "QMainWindow::separator { width: 8px; height: 8px; "
        "background: transparent; }"
        "QMainWindow::separator:hover, QMainWindow::separator:pressed { "
        "background: palette(highlight); }"));
  }
  // QMainWindow creates its dock separators lazily when the window is laid
  // out.  Keep the hit target wide enough for a mouse even when a native
  // style would otherwise expose only a one-pixel separator.
  window.setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                        QMainWindow::AllowTabbedDocks |
                        QMainWindow::GroupedDragging);
  window.setDockNestingEnabled(true);
  // Keep the side areas as the primary drop targets for the two inspector
  // docks.  Without explicit corner ownership, a native floating dock can
  // cross the main-window edge without the right-side drop indicator being
  // activated on some platform styles.
  window.setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  window.setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  window.setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  window.setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

  widgets.center_splitter = new QSplitter(Qt::Vertical, &window);
  widgets.center_splitter->setObjectName(QStringLiteral("previewHexSplitter"));
  widgets.center_splitter->setChildrenCollapsible(false);

  widgets.preview_tabs = new QTabWidget(widgets.center_splitter);
  widgets.preview_tabs->setObjectName(QStringLiteral("previewTabs"));
  widgets.preview_tabs->setAccessibleName(QStringLiteral("Preview stages"));
  widgets.preview_tabs->setUsesScrollButtons(true);
  widgets.image_view =
      new pnga::ui::qt::DeliveredImageView(widgets.preview_tabs);
  widgets.preview_tabs->addTab(widgets.image_view, QStringLiteral("Image"));
  widgets.pixel_view = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kNative,
      widgets.preview_tabs);
  widgets.preview_tabs->addTab(widgets.pixel_view, QStringLiteral("Pixels"));
  widgets.filtered_view = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kFiltered,
      widgets.preview_tabs);
  widgets.preview_tabs->addTab(widgets.filtered_view, QStringLiteral("Filtered"));
  widgets.defiltered_view = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kDefiltered,
      widgets.preview_tabs);
  widgets.preview_tabs->addTab(widgets.defiltered_view,
                               QStringLiteral("Unfiltered"));

  widgets.hex_panel = new QWidget(widgets.center_splitter);
  widgets.hex_panel->setObjectName(QStringLiteral("hexPanel"));
  widgets.hex_panel->setAccessibleName(QStringLiteral("Hex panel"));
  auto* hex_layout = new QHBoxLayout(widgets.hex_panel);
  hex_layout->setContentsMargins(0, 0, 0, 0);
  widgets.hex_source_tabs =
      new pnga::ui::qt::HexSourceTabBar(widgets.hex_panel);
  hex_layout->addWidget(widgets.hex_source_tabs);
  widgets.hex = new pnga::ui::qt::HexView(widgets.hex_panel);
  widgets.hex->setObjectName(QStringLiteral("hexView"));
  widgets.hex->setAccessibleName(QStringLiteral("Hex view"));
  hex_layout->addWidget(widgets.hex, 1);
  widgets.center_splitter->addWidget(widgets.preview_tabs);
  widgets.center_splitter->addWidget(widgets.hex_panel);
  widgets.center_splitter->setStretchFactor(0, 3);
  widgets.center_splitter->setStretchFactor(1, 2);
  window.setCentralWidget(widgets.center_splitter);

  widgets.chunks_dock = new QDockWidget(QStringLiteral("Chunks"), &window);
  widgets.chunks_dock->setObjectName(QStringLiteral("chunksDock"));
  // A floated dock must be able to re-enter through any visible dock area.
  // The default placement remains on the left; allowing all areas avoids a
  // platform-specific failure to recognize the docking target while dragging
  // the native floating title bar back over the main window.
  widgets.chunks_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
  widgets.chunks_dock->setFeatures(QDockWidget::DockWidgetMovable |
                                   QDockWidget::DockWidgetFloatable |
                                   QDockWidget::DockWidgetClosable);
  widgets.chunks_splitter = new QSplitter(Qt::Vertical, widgets.chunks_dock);
  widgets.chunks_splitter->setObjectName(QStringLiteral("chunksDetailSplitter"));
  widgets.chunks_splitter->setChildrenCollapsible(false);
  widgets.chunks_splitter->setHandleWidth(8);
  widgets.tree = new QTreeView(widgets.chunks_splitter);
  widgets.tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  widgets.tree->setSelectionMode(QAbstractItemView::SingleSelection);
  widgets.tree->setUniformRowHeights(true);
  // Do not let a newly loaded model turn its largest offset/type cell into a
  // dock minimum width.  The view is allowed to shrink and uses a horizontal
  // scrollbar for long values; users can still resize columns interactively.
  widgets.tree->setMinimumWidth(0);
  widgets.tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  widgets.tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  widgets.tree->header()->setSectionResizeMode(QHeaderView::Interactive);
  widgets.tree->header()->setStretchLastSection(true);
  widgets.tree->setMinimumHeight(80);
  widgets.chunks_splitter->addWidget(widgets.tree);
  widgets.chunk_detail =
      new pnga::ui::qt::ChunkDetailPanel(widgets.chunks_splitter);
  widgets.chunk_detail->setMinimumHeight(80);
  widgets.chunks_splitter->addWidget(widgets.chunk_detail);
  widgets.chunks_splitter->setStretchFactor(0, 3);
  widgets.chunks_splitter->setStretchFactor(1, 2);
  widgets.chunks_splitter->setSizes({360, 180});
  widgets.chunks_dock->setWidget(widgets.chunks_splitter);
  window.addDockWidget(Qt::LeftDockWidgetArea, widgets.chunks_dock);

  widgets.bus = new pnga::ui::qt::SelectionBus(&window);

  widgets.inspector_dock =
      new QDockWidget(QStringLiteral("Inspector"), &window);
  widgets.inspector_dock->setObjectName(QStringLiteral("inspectorDock"));
  widgets.inspector_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
  widgets.inspector_dock->setFeatures(QDockWidget::DockWidgetMovable |
                                      QDockWidget::DockWidgetFloatable |
                                      QDockWidget::DockWidgetClosable);
  auto* inspector_container = new QWidget(widgets.inspector_dock);
  auto* inspector_layout = new QVBoxLayout(inspector_container);
  inspector_layout->setContentsMargins(6, 6, 6, 6);
  auto* coordinate_bar = new QWidget(inspector_container);
  coordinate_bar->setObjectName(QStringLiteral("coordinateToolbar"));
  coordinate_bar->setAccessibleName(QStringLiteral("Coordinate toolbar"));
  auto* coordinate_layout = new QHBoxLayout(coordinate_bar);
  coordinate_layout->setContentsMargins(0, 0, 0, 0);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("X"), coordinate_bar));
  widgets.x_spin = new QSpinBox(coordinate_bar);
  widgets.x_spin->setObjectName(QStringLiteral("xCoordinate"));
  widgets.x_spin->setAccessibleName(QStringLiteral("X coordinate"));
  widgets.x_spin->setRange(0, std::numeric_limits<int>::max());
  widgets.x_spin->setFixedWidth(
      std::max(1, widgets.x_spin->sizeHint().width() * 2 / 3));
  coordinate_layout->addWidget(widgets.x_spin);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("Y"), coordinate_bar));
  widgets.y_spin = new QSpinBox(coordinate_bar);
  widgets.y_spin->setObjectName(QStringLiteral("yCoordinate"));
  widgets.y_spin->setAccessibleName(QStringLiteral("Y coordinate"));
  widgets.y_spin->setRange(0, std::numeric_limits<int>::max());
  widgets.y_spin->setFixedWidth(
      std::max(1, widgets.y_spin->sizeHint().width() * 2 / 3));
  coordinate_layout->addWidget(widgets.y_spin);
  widgets.lock_check = new QCheckBox(QStringLiteral("Lock"), coordinate_bar);
  widgets.lock_check->setObjectName(QStringLiteral("lockCoordinate"));
  widgets.lock_check->setAccessibleName(QStringLiteral("Lock coordinate"));
  coordinate_layout->addWidget(widgets.lock_check);
  widgets.base_button = new QPushButton(QStringLiteral("HEX"), coordinate_bar);
  widgets.base_button->setObjectName(QStringLiteral("numericBase"));
  widgets.base_button->setAccessibleName(QStringLiteral("Numeric base toggle"));
  widgets.base_button->setFlat(true);
  widgets.base_button->setAutoDefault(false);
  widgets.base_button->setCursor(Qt::PointingHandCursor);
  widgets.base_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  widgets.base_button->setFixedWidth(widgets.base_button->sizeHint().width());
  coordinate_layout->addWidget(widgets.base_button);
  coordinate_layout->addStretch(1);
  // Keep the toolbar controls on one stable row without allowing their
  // combined size hint to become the Inspector's minimum width.  The toolbar
  // itself scrolls horizontally when the dock is narrower than its controls;
  // the report and page tabs remain independently scrollable below it.
  coordinate_bar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  auto* coordinate_scroll = new QScrollArea(inspector_container);
  coordinate_scroll->setObjectName(QStringLiteral("coordinateToolbarScroll"));
  coordinate_scroll->setAccessibleName(
      QStringLiteral("Coordinate toolbar scroll area"));
  coordinate_scroll->setFrameShape(QFrame::NoFrame);
  coordinate_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  coordinate_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  coordinate_scroll->setWidgetResizable(false);
  coordinate_scroll->setMinimumWidth(0);
  coordinate_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  coordinate_scroll->setWidget(coordinate_bar);
  coordinate_scroll->setFixedHeight(
      coordinate_bar->sizeHint().height() +
      coordinate_scroll->horizontalScrollBar()->sizeHint().height());
  inspector_layout->addWidget(coordinate_scroll);

  // Inspector uses stable primary groups with contextual secondary pages.
  // Pages are created once and only the selected group/page changes, so
  // switching context never changes the dock width.
  widgets.inspector_tabs = new QTabWidget(inspector_container);
  widgets.inspector_tabs->setObjectName(QStringLiteral("inspectorTabs"));
  widgets.inspector_tabs->setAccessibleName(QStringLiteral("Inspector groups"));
  widgets.inspector_tabs->setUsesScrollButtons(true);

  widgets.compression_inspector_tabs = new QTabWidget(inspector_container);
  widgets.compression_inspector_tabs->setObjectName(
      QStringLiteral("compressionInspectorPages"));
  widgets.compression_inspector_tabs->setAccessibleName(
      QStringLiteral("Compression inspector pages"));
  widgets.compression_inspector_tabs->setUsesScrollButtons(true);

  widgets.inspector = new pnga::ui::qt::StageInspector(widgets.inspector_tabs);
  widgets.inspector->setObjectName(QStringLiteral("reconstructInspector"));
  widgets.inspector->setAccessibleName(QStringLiteral("Reconstruct inspector"));
  widgets.inspector_tabs->addTab(widgets.inspector,
                                 QStringLiteral("Reconstruction"));
  widgets.block_inspector =
      new pnga::ui::qt::BlockInspector(widgets.compression_inspector_tabs);
  widgets.block_inspector->setObjectName(QStringLiteral("blockInspector"));
  widgets.block_inspector->setAccessibleName(
      QStringLiteral("DEFLATE block inspector"));
  widgets.compression_inspector_tabs->addTab(widgets.block_inspector,
                                             QStringLiteral("DEFLATE Blocks"));
  widgets.huffman_inspector =
      new pnga::ui::qt::HuffmanInspector(widgets.compression_inspector_tabs);
  widgets.huffman_inspector->setObjectName(QStringLiteral("huffmanInspector"));
  widgets.huffman_inspector->setAccessibleName(
      QStringLiteral("DEFLATE Huffman table inspector"));
  widgets.compression_inspector_tabs->addTab(widgets.huffman_inspector,
                                             QStringLiteral("Huffman"));
  widgets.decode_trace_inspector =
      new pnga::ui::qt::DecodeTraceInspector(widgets.compression_inspector_tabs);
  widgets.decode_trace_inspector->setObjectName(
      QStringLiteral("decodeTraceInspector"));
  widgets.decode_trace_inspector->setAccessibleName(
      QStringLiteral("DEFLATE decode trace inspector"));
  widgets.compression_inspector_tabs->addTab(widgets.decode_trace_inspector,
                                             QStringLiteral("Decode Trace"));
  // WP-5U12: the shared Compression context sits above the page stack so the
  // trace state is stated once, not repeated on every page.
  auto* compression_container = new QWidget(inspector_container);
  compression_container->setObjectName(QStringLiteral("compressionContainer"));
  compression_container->setAccessibleName(
      QStringLiteral("Compression inspector"));
  auto* compression_layout = new QVBoxLayout(compression_container);
  compression_layout->setContentsMargins(4, 2, 4, 2);
  compression_layout->setSpacing(2);
  widgets.compression_context =
      new pnga::ui::qt::CompressionContext(compression_container);
  compression_layout->addWidget(widgets.compression_context);
  compression_layout->addWidget(widgets.compression_inspector_tabs, 1);
  widgets.inspector_tabs->addTab(compression_container,
                                 QStringLiteral("Compression"));
  // WP-5U13: bind the bounded trace pipeline to the three Compression pages.
  // The binding publishes one generation-coherent bundle; navigation keeps the
  // WP-5U11 source semantics (physical File for block spans, Inflated for
  // output bytes, IDAT for logical Deflate bits). Behavior wiring stays in
  // MainWindow; the builder only creates the binding and its initial state.
  widgets.trace_binding = new pnga::ui::qt::TraceInspectorBinding(
      widgets.block_inspector, widgets.huffman_inspector,
      widgets.decode_trace_inspector, &window);
  widgets.trace_binding->setContext(widgets.compression_context);
  widgets.trace_binding->setHasDocument(false);
  inspector_layout->addWidget(widgets.inspector_tabs, 1);
  QWidget::setTabOrder(widgets.x_spin, widgets.y_spin);
  QWidget::setTabOrder(widgets.y_spin, widgets.lock_check);
  QWidget::setTabOrder(widgets.lock_check, widgets.base_button);
  QWidget::setTabOrder(widgets.base_button, widgets.preview_tabs);
  QWidget::setTabOrder(widgets.preview_tabs, widgets.hex_source_tabs);
  QWidget::setTabOrder(widgets.hex_source_tabs, widgets.hex);
  QWidget::setTabOrder(widgets.hex, widgets.inspector_tabs);
  widgets.inspector_dock->setWidget(inspector_container);
  window.addDockWidget(Qt::RightDockWidgetArea, widgets.inspector_dock);

  widgets.pixel_label = new QLabel(QStringLiteral("No image"), &window);
  widgets.pixel_label->setObjectName(QStringLiteral("pixelStatus"));
  window.statusBar()->addWidget(widgets.pixel_label);
  widgets.validation_label =
      new QLabel(QStringLiteral("Validation: not loaded"), &window);
  widgets.validation_label->setObjectName(QStringLiteral("validationStatus"));
  widgets.validation_label->setAccessibleName(
      QStringLiteral("Validation status"));
  window.statusBar()->addPermanentWidget(widgets.validation_label);

  QMenu* fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
  widgets.open_action = fileMenu->addAction(QStringLiteral("&Open..."));
  widgets.open_action->setShortcut(QKeySequence::Open);
  widgets.close_action = fileMenu->addAction(QStringLiteral("&Close Image"));
  widgets.close_action->setObjectName(QStringLiteral("closeImageAction"));
  widgets.close_action->setShortcut(QKeySequence::Close);
  widgets.close_action->setEnabled(false);
  fileMenu->addSeparator();
  widgets.recent_files_menu = fileMenu->addMenu(QStringLiteral("Open Recent"));
  widgets.recent_files_menu->setObjectName(QStringLiteral("recentFilesMenu"));
  widgets.recent_files_menu->setToolTipsVisible(true);
  fileMenu->addSeparator();
  widgets.exit_action = fileMenu->addAction(QStringLiteral("E&xit"));
  widgets.exit_action->setObjectName(QStringLiteral("exitAction"));
  widgets.exit_action->setShortcut(QKeySequence::Quit);

  QMenu* viewMenu = window.menuBar()->addMenu(QStringLiteral("&View"));
  widgets.reset_layout_action =
      viewMenu->addAction(QStringLiteral("&Reset Layout"));
  viewMenu->addSeparator();
  widgets.show_chunks_action = widgets.chunks_dock->toggleViewAction();
  viewMenu->addAction(widgets.show_chunks_action);
  widgets.show_chunks_action->setText(QStringLiteral("Chunk List"));
  widgets.show_chunks_action->setObjectName(QStringLiteral("showChunkList"));
  widgets.show_hex_view_action =
      viewMenu->addAction(QStringLiteral("Hex View"));
  widgets.show_hex_view_action->setObjectName(QStringLiteral("showHexView"));
  widgets.show_hex_view_action->setCheckable(true);
  widgets.show_hex_view_action->setChecked(true);
  widgets.show_inspector_action = widgets.inspector_dock->toggleViewAction();
  viewMenu->addAction(widgets.show_inspector_action);
  widgets.show_inspector_action->setText(QStringLiteral("Inspector"));
  widgets.show_inspector_action->setObjectName(QStringLiteral("showInspector"));
  viewMenu->addSeparator();
  if (theme != nullptr) {
    QMenu* themeMenu = viewMenu->addMenu(QStringLiteral("Theme"));
    themeMenu->setObjectName(QStringLiteral("themeMenu"));
    auto* group = new QActionGroup(themeMenu);
    group->setExclusive(true);
    const auto addThemeAction = [themeMenu, group](
                                    const QString& text,
                                    const QString& object_name,
                                    pnga::ui::qt::ApplicationTheme::ThemeMode
                                        mode) {
      QAction* action = themeMenu->addAction(text);
      action->setObjectName(object_name);
      action->setCheckable(true);
      action->setData(static_cast<int>(mode));
      group->addAction(action);
      return action;
    };
    widgets.theme_system_action = addThemeAction(
        QStringLiteral("Follow System"), QStringLiteral("themeSystem"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
    widgets.theme_light_action = addThemeAction(
        QStringLiteral("Light"), QStringLiteral("themeLight"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
    widgets.theme_dark_action = addThemeAction(
        QStringLiteral("Dark"), QStringLiteral("themeDark"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
  }

  return widgets;
}
