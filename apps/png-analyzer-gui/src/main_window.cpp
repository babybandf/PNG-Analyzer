// WP-104/204 MainWindow: docks, menu, file open, chunk->hex selection and the
// background reference decode into the delivered image view.

#include "main_window.h"

#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_detail_panel.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/hex_data_source.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/pixel_viewport.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_preview_view.h>

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QMetaObject>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QScrollBar>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <QSettings>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <iterator>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;
constexpr int kMaxRecentFiles = 10;
constexpr auto kRecentFilesSettingsKey = "file/recentFiles";
constexpr auto kLastOpenDirectorySettingsKey = "file/lastOpenDirectory";

}  // namespace

// ---------------------------------------------------------------------------
// DecodeWorker
// ---------------------------------------------------------------------------

DecodeWorker::DecodeWorker(std::uint64_t generation,
                           std::shared_ptr<pnga::io::IByteSource> source,
                           QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)) {}

void DecodeWorker::run() {
  result_ = pnga::analysis_engine::decode_reference(*source_);
  emit decodeDone(generation_);
}

// ---------------------------------------------------------------------------
// StageWorker
// ---------------------------------------------------------------------------

StageWorker::StageWorker(std::uint64_t generation,
                         std::shared_ptr<pnga::io::IByteSource> source,
                         QObject* parent)
    : QThread(parent), generation_(generation), source_(std::move(source)) {}

void StageWorker::run() {
  result_ = std::make_shared<pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source_));
  emit stageDone(generation_);
}

ValidationWorker::ValidationWorker(
    std::uint64_t generation, std::shared_ptr<pnga::io::IByteSource> source,
    pnga::png_format::ChunkIndex index, QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)),
      index_(std::move(index)) {}

void ValidationWorker::run() {
  result_ = pnga::analysis_engine::validate_document(*source_, index_);
  emit validationDone(generation_);
}

ChunkDetailWorker::ChunkDetailWorker(
    std::uint64_t generation, std::uint64_t selection_serial,
    std::shared_ptr<pnga::io::IByteSource> source,
    pnga::png_format::ChunkNode node, QObject* parent)
    : QThread(parent),
      generation_(generation),
      selection_serial_(selection_serial),
      source_(std::move(source)),
      node_(node) {}

void ChunkDetailWorker::run() {
  result_ = pnga::png_format::describe_chunk(*source_, node_);
  emit detailDone(generation_, selection_serial_);
}

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("PNG Analyzer"));
  // QMainWindow creates its dock separators lazily when the window is laid
  // out.  Keep the hit target wide enough for a mouse even when a native
  // style would otherwise expose only a one-pixel separator.
  setStyleSheet(QStringLiteral(
      "QMainWindow::separator { width: 8px; height: 8px; "
      "background: transparent; }"
      "QMainWindow::separator:hover, QMainWindow::separator:pressed { "
      "background: palette(highlight); }"));
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks |
                 QMainWindow::GroupedDragging);
  setDockNestingEnabled(true);
  // Keep the side areas as the primary drop targets for the two inspector
  // docks.  Without explicit corner ownership, a native floating dock can
  // cross the main-window edge without the right-side drop indicator being
  // activated on some platform styles.
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

  center_splitter_ = new QSplitter(Qt::Vertical, this);
  center_splitter_->setObjectName(QStringLiteral("previewHexSplitter"));
  center_splitter_->setChildrenCollapsible(false);

  preview_tabs_ = new QTabWidget(center_splitter_);
  preview_tabs_->setObjectName(QStringLiteral("previewTabs"));
  preview_tabs_->setAccessibleName(QStringLiteral("Preview stages"));
  preview_tabs_->setUsesScrollButtons(true);
  image_view_ = new pnga::ui::qt::DeliveredImageView(preview_tabs_);
  preview_tabs_->addTab(image_view_, QStringLiteral("Image"));
  pixel_view_ = new pnga::ui::qt::PixelViewport(preview_tabs_);
  preview_tabs_->addTab(pixel_view_, QStringLiteral("Pixels"));
  filter_map_view_ = new pnga::ui::qt::StagePreviewView(
      pnga::ui::qt::PreviewStage::kFilterMap, preview_tabs_);
  preview_tabs_->addTab(filter_map_view_, QStringLiteral("Filter Map"));
  filtered_view_ = new pnga::ui::qt::StagePreviewView(
      pnga::ui::qt::PreviewStage::kFiltered, preview_tabs_);
  preview_tabs_->addTab(filtered_view_, QStringLiteral("Filtered"));
  defiltered_view_ = new pnga::ui::qt::StagePreviewView(
      pnga::ui::qt::PreviewStage::kDefiltered, preview_tabs_);
  preview_tabs_->addTab(defiltered_view_, QStringLiteral("Defiltered"));

  hex_ = new pnga::ui::qt::HexView(center_splitter_);
  hex_->setObjectName(QStringLiteral("hexView"));
  hex_->setAccessibleName(QStringLiteral("Hex view"));
  center_splitter_->addWidget(preview_tabs_);
  center_splitter_->addWidget(hex_);
  center_splitter_->setStretchFactor(0, 3);
  center_splitter_->setStretchFactor(1, 2);
  setCentralWidget(center_splitter_);

  chunks_dock_ = new QDockWidget(QStringLiteral("Chunks"), this);
  chunks_dock_->setObjectName(QStringLiteral("chunksDock"));
  // A floated dock must be able to re-enter through any visible dock area.
  // The default placement remains on the left; allowing all areas avoids a
  // platform-specific failure to recognize the docking target while dragging
  // the native floating title bar back over the main window.
  chunks_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  chunks_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
  chunks_splitter_ = new QSplitter(Qt::Vertical, chunks_dock_);
  chunks_splitter_->setObjectName(QStringLiteral("chunksDetailSplitter"));
  chunks_splitter_->setChildrenCollapsible(false);
  chunks_splitter_->setHandleWidth(8);
  tree_ = new QTreeView(chunks_splitter_);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setUniformRowHeights(true);
  // Do not let a newly loaded model turn its largest offset/type cell into a
  // dock minimum width.  The view is allowed to shrink and uses a horizontal
  // scrollbar for long values; users can still resize columns interactively.
  tree_->setMinimumWidth(0);
  tree_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
  tree_->header()->setStretchLastSection(true);
  tree_->setMinimumHeight(80);
  chunks_splitter_->addWidget(tree_);
  chunk_detail_ = new pnga::ui::qt::ChunkDetailPanel(chunks_splitter_);
  chunk_detail_->setMinimumHeight(80);
  chunks_splitter_->addWidget(chunk_detail_);
  chunks_splitter_->setStretchFactor(0, 3);
  chunks_splitter_->setStretchFactor(1, 2);
  chunks_splitter_->setSizes({360, 180});
  chunks_dock_->setWidget(chunks_splitter_);
  addDockWidget(Qt::LeftDockWidgetArea, chunks_dock_);

  bus_ = new pnga::ui::qt::SelectionBus(this);

  inspector_dock_ = new QDockWidget(QStringLiteral("Inspector"), this);
  inspector_dock_->setObjectName(QStringLiteral("inspectorDock"));
  inspector_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  inspector_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);
  auto* inspector_container = new QWidget(inspector_dock_);
  auto* inspector_layout = new QVBoxLayout(inspector_container);
  inspector_layout->setContentsMargins(6, 6, 6, 6);
  auto* coordinate_bar = new QWidget(inspector_container);
  coordinate_bar->setObjectName(QStringLiteral("coordinateToolbar"));
  coordinate_bar->setAccessibleName(QStringLiteral("Coordinate toolbar"));
  auto* coordinate_layout = new QHBoxLayout(coordinate_bar);
  coordinate_layout->setContentsMargins(0, 0, 0, 0);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("X"), coordinate_bar));
  x_spin_ = new QSpinBox(coordinate_bar);
  x_spin_->setObjectName(QStringLiteral("xCoordinate"));
  x_spin_->setAccessibleName(QStringLiteral("X coordinate"));
  x_spin_->setRange(0, std::numeric_limits<int>::max());
  x_spin_->setFixedWidth(std::max(1, x_spin_->sizeHint().width() * 2 / 3));
  coordinate_layout->addWidget(x_spin_);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("Y"), coordinate_bar));
  y_spin_ = new QSpinBox(coordinate_bar);
  y_spin_->setObjectName(QStringLiteral("yCoordinate"));
  y_spin_->setAccessibleName(QStringLiteral("Y coordinate"));
  y_spin_->setRange(0, std::numeric_limits<int>::max());
  y_spin_->setFixedWidth(std::max(1, y_spin_->sizeHint().width() * 2 / 3));
  coordinate_layout->addWidget(y_spin_);
  lock_check_ = new QCheckBox(QStringLiteral("Lock"), coordinate_bar);
  lock_check_->setObjectName(QStringLiteral("lockCoordinate"));
  lock_check_->setAccessibleName(QStringLiteral("Lock coordinate"));
  coordinate_layout->addWidget(lock_check_);
  base_button_ = new QPushButton(QStringLiteral("HEX"), coordinate_bar);
  base_button_->setObjectName(QStringLiteral("numericBase"));
  base_button_->setAccessibleName(QStringLiteral("Numeric base toggle"));
  base_button_->setFlat(true);
  base_button_->setAutoDefault(false);
  base_button_->setCursor(Qt::PointingHandCursor);
  base_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  base_button_->setFixedWidth(
      base_button_->fontMetrics().horizontalAdvance(QStringLiteral("DEC")) +
      8);
  coordinate_layout->addWidget(base_button_);
  hex_source_combo_ = new QComboBox(coordinate_bar);
  hex_source_combo_->setObjectName(QStringLiteral("hexSource"));
  hex_source_combo_->setAccessibleName(QStringLiteral("Hex source"));
  hex_source_combo_->addItem(QStringLiteral("File"));
  hex_source_combo_->addItem(QStringLiteral("IDAT Stream"));
  hex_source_combo_->addItem(QStringLiteral("Inflated"));
  hex_source_combo_->addItem(QStringLiteral("Defiltered"));
  coordinate_layout->addWidget(hex_source_combo_);
  hex_follow_check_ = new QCheckBox(QStringLiteral("Hex follows pixel"),
                                    coordinate_bar);
  hex_follow_check_->setObjectName(QStringLiteral("hexFollowPixel"));
  hex_follow_check_->setAccessibleName(QStringLiteral("Hex follows pixel"));
  coordinate_layout->addWidget(hex_follow_check_);
  coordinate_layout->addStretch(1);
  // Keep the toolbar controls on one stable row without allowing their
  // combined size hint to become the Inspector's minimum width.  The toolbar
  // itself scrolls horizontally when the dock is narrower than its controls;
  // the report and page tabs remain independently scrollable below it.
  coordinate_bar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  auto* coordinate_scroll = new QScrollArea(inspector_container);
  coordinate_scroll->setObjectName(QStringLiteral("coordinateToolbarScroll"));
  coordinate_scroll->setAccessibleName(QStringLiteral("Coordinate toolbar scroll area"));
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
  inspector_tabs_ = new QTabWidget(inspector_container);
  inspector_tabs_->setObjectName(QStringLiteral("inspectorTabs"));
  inspector_tabs_->setAccessibleName(QStringLiteral("Inspector groups"));
  inspector_tabs_->setUsesScrollButtons(true);

  const auto makePageTabs = [inspector_container](const QString& name,
                                                   const QString& accessible) {
    auto* tabs = new QTabWidget(inspector_container);
    tabs->setObjectName(name);
    tabs->setAccessibleName(accessible);
    tabs->setUsesScrollButtons(true);
    return tabs;
  };
  image_inspector_tabs_ = makePageTabs(QStringLiteral("imageInspectorPages"),
                                       QStringLiteral("Image inspector pages"));
  scanline_inspector_tabs_ = makePageTabs(
      QStringLiteral("scanlineInspectorPages"),
      QStringLiteral("Scanline inspector pages"));
  compression_inspector_tabs_ = makePageTabs(
      QStringLiteral("compressionInspectorPages"),
      QStringLiteral("Compression inspector pages"));

  inspector_ = new pnga::ui::qt::StageInspector(image_inspector_tabs_);
  inspector_->setObjectName(QStringLiteral("reconstructInspector"));
  inspector_->setAccessibleName(QStringLiteral("Reconstruct inspector"));
  image_inspector_tabs_->addTab(inspector_, QStringLiteral("Reconstruction"));
  const auto addInspectorPlaceholder = [](QTabWidget* tabs,
                                           const QString& title,
                                           const QString& object_name,
                                           const QString& accessible_name) {
    auto* label = new QLabel(QStringLiteral("Not available for current selection"),
                             tabs);
    label->setObjectName(object_name);
    label->setAccessibleName(accessible_name);
    label->setAlignment(Qt::AlignCenter);
    tabs->addTab(label, title);
  };
  addInspectorPlaceholder(image_inspector_tabs_, QStringLiteral("Pixel"),
                          QStringLiteral("pixelInspector"), QStringLiteral("Pixel inspector"));
  addInspectorPlaceholder(image_inspector_tabs_, QStringLiteral("Format Context"),
                          QStringLiteral("formatContextInspector"), QStringLiteral("Format context inspector"));
  addInspectorPlaceholder(scanline_inspector_tabs_, QStringLiteral("Scanline"),
                          QStringLiteral("scanlineInspector"), QStringLiteral("Scanline inspector"));
  addInspectorPlaceholder(scanline_inspector_tabs_, QStringLiteral("Source"),
                          QStringLiteral("sourceInspector"), QStringLiteral("Source inspector"));
  block_inspector_ = new pnga::ui::qt::BlockInspector(compression_inspector_tabs_);
  block_inspector_->setObjectName(QStringLiteral("blockInspector"));
  block_inspector_->setAccessibleName(QStringLiteral("DEFLATE block inspector"));
  compression_inspector_tabs_->addTab(block_inspector_, QStringLiteral("DEFLATE Blocks"));
  huffman_inspector_ = new pnga::ui::qt::HuffmanInspector(compression_inspector_tabs_);
  huffman_inspector_->setObjectName(QStringLiteral("huffmanInspector"));
  huffman_inspector_->setAccessibleName(
      QStringLiteral("DEFLATE Huffman table inspector"));
  compression_inspector_tabs_->addTab(huffman_inspector_, QStringLiteral("Huffman"));
  decode_trace_inspector_ =
      new pnga::ui::qt::DecodeTraceInspector(compression_inspector_tabs_);
  decode_trace_inspector_->setObjectName(QStringLiteral("decodeTraceInspector"));
  decode_trace_inspector_->setAccessibleName(
      QStringLiteral("DEFLATE decode trace inspector"));
  compression_inspector_tabs_->addTab(decode_trace_inspector_, QStringLiteral("Decode Trace"));
  inspector_tabs_->addTab(image_inspector_tabs_, QStringLiteral("Image"));
  inspector_tabs_->addTab(scanline_inspector_tabs_, QStringLiteral("Scanline"));
  inspector_tabs_->addTab(compression_inspector_tabs_, QStringLiteral("Compression"));
  inspector_layout->addWidget(inspector_tabs_, 1);
  QWidget::setTabOrder(x_spin_, y_spin_);
  QWidget::setTabOrder(y_spin_, lock_check_);
  QWidget::setTabOrder(lock_check_, base_button_);
  QWidget::setTabOrder(base_button_, hex_source_combo_);
  QWidget::setTabOrder(hex_source_combo_, hex_follow_check_);
  QWidget::setTabOrder(hex_follow_check_, inspector_tabs_);
  inspector_dock_->setWidget(inspector_container);
  addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

  pixel_label_ = new QLabel(QStringLiteral("No image"), this);
  pixel_label_->setObjectName(QStringLiteral("pixelStatus"));
  statusBar()->addWidget(pixel_label_);
  validation_label_ = new QLabel(QStringLiteral("Validation: not loaded"), this);
  validation_label_->setObjectName(QStringLiteral("validationStatus"));
  validation_label_->setAccessibleName(QStringLiteral("Validation status"));
  statusBar()->addPermanentWidget(validation_label_);

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);
  fileMenu->addSeparator();
  recent_files_menu_ = fileMenu->addMenu(QStringLiteral("Open Recent"));
  recent_files_menu_->setObjectName(QStringLiteral("recentFilesMenu"));
  recent_files_menu_->setToolTipsVisible(true);
  connect(recent_files_menu_, &QMenu::aboutToShow, this,
          &MainWindow::refreshRecentFilesMenu);
  refreshRecentFilesMenu();

  QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
  QAction* resetAction =
      viewMenu->addAction(QStringLiteral("&Reset Layout"));
  connect(resetAction, &QAction::triggered, this, &MainWindow::resetLayout);

  QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  helpMenu->addAction(QStringLiteral("About"), this, [this] {
    pnga::ui::qt::AboutDialog dialog(
        pnga::ui::qt::default_about_content(), this);
    dialog.exec();
  });

  // Initial empty model so the selection-model connection is valid from the
  // start; resetDocument() reconnects after each real setModel.
  model_ = new pnga::ui::qt::ChunkModel(&index_, this);
  tree_->setModel(model_);
  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onChunkSelectionChanged);

  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelSelected,
          this, &MainWindow::onPixelSelected);
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHovered,
          this, [this](int x, int y) {
            const pnga::trace_model::ImageCoordinate coordinate{
                0, 0, 0, static_cast<std::uint64_t>(x),
                static_cast<std::uint64_t>(y)};
            view_state_.set_hover(coordinate);
            setPixelStatus(x, y);
          });
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
          this, [this] {
            view_state_.clear_hover();
            restorePixelStatus();
          });
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested, this,
          &MainWindow::nudgeLockedCoordinate);
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::selectionCancelled, this,
          &MainWindow::clearLockedCoordinate);
  x_spin_->installEventFilter(this);
  y_spin_->installEventFilter(this);
  lock_check_->installEventFilter(this);
  base_button_->installEventFilter(this);
  hex_follow_check_->installEventFilter(this);
  preview_tabs_->installEventFilter(this);
  inspector_tabs_->installEventFilter(this);

  query_bridge_ = new QueryStatusBridge(this);
  connect(query_bridge_, &QueryStatusBridge::rowStatus, this,
          &MainWindow::onRowQueryStatus);

  connect(x_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (lock_check_->isChecked()) {
              publishLockedCoordinate();
            }
          });
  connect(y_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (lock_check_->isChecked()) {
              publishLockedCoordinate();
            }
          });
  connect(lock_check_, &QCheckBox::toggled, this, [this](bool locked) {
    if (locked) {
      publishLockedCoordinate();
    } else {
      clearLockedCoordinate();
    }
  });
  connect(base_button_, &QPushButton::clicked, this, [this](bool) {
    const bool hexadecimal =
        view_state_.numeric_base != pnga::ui::qt::NumericBase::kHexadecimal;
    view_state_.numeric_base =
        hexadecimal ? pnga::ui::qt::NumericBase::kHexadecimal
                    : pnga::ui::qt::NumericBase::kDecimal;
    inspector_->setNumericBase(hexadecimal);
    updateNumericBaseButton();
  });
  connect(hex_follow_check_, &QCheckBox::toggled, this,
          [this](bool checked) { view_state_.hex_follow_pixel = checked; });
  connect(hex_source_combo_,
          qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            view_state_.hex_source = index == 1
                                         ? pnga::ui::qt::HexSource::kIdatStream
                                         : index == 2
                                               ? pnga::ui::qt::HexSource::kInflated
                                               : index == 3
                                                     ? pnga::ui::qt::HexSource::kDefiltered
                                                     : pnga::ui::qt::HexSource::kFile;
            updateHexSource();
          });

  resize(1200, 760);
  applyDefaultWorkspace();
  restoreWorkspace();
  configureDockInteraction();
}

void MainWindow::applyDefaultWorkspace() {
  const auto preserved_locked = view_state_.locked;
  const auto preserved_hover = view_state_.hover;
  // Reset Layout must restore the dock topology as well as its dimensions.
  // addDockWidget() selects the target side, while setFloating(false) is
  // required for Qt to leave a native floating window on every platform.
  addDockWidget(Qt::LeftDockWidgetArea, chunks_dock_);
  chunks_dock_->setFloating(false);
  addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
  inspector_dock_->setFloating(false);
  resize(1200, 760);
  center_splitter_->setSizes({456, 304});
  chunks_splitter_->setSizes({360, 180});
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  image_inspector_tabs_->setCurrentIndex(0);
  scanline_inspector_tabs_->setCurrentIndex(0);
  compression_inspector_tabs_->setCurrentIndex(0);
  chunks_dock_->show();
  inspector_dock_->show();
  chunks_dock_->setMinimumWidth(180);
  inspector_dock_->setMinimumWidth(260);
  resizeDocks({chunks_dock_, inspector_dock_}, {240, 420},
              Qt::Horizontal);
  setMinimumSize(840, 520);
  for (auto* splitter : findChildren<QSplitter*>()) {
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
  }
  configureDockInteraction();

  view_state_.hex_source = pnga::ui::qt::HexSource::kFile;
  view_state_.numeric_base = pnga::ui::qt::NumericBase::kDecimal;
  view_state_.hex_follow_pixel = true;
  view_state_.locked = preserved_locked;
  view_state_.hover = preserved_hover;
  {
    const QSignalBlocker base_blocker(base_button_);
    const QSignalBlocker source_blocker(hex_source_combo_);
    const QSignalBlocker follow_blocker(hex_follow_check_);
    const QSignalBlocker lock_blocker(lock_check_);
    updateNumericBaseButton();
    hex_source_combo_->setCurrentIndex(0);
    hex_follow_check_->setChecked(true);
    lock_check_->setChecked(preserved_locked.has_value());
    if (preserved_locked.has_value()) {
      x_spin_->setValue(static_cast<int>(preserved_locked->x));
      y_spin_->setValue(static_cast<int>(preserved_locked->y));
    }
  }
  inspector_->setNumericBase(false);
  updateHexSource();
}

void MainWindow::configureDockInteraction() {
  if (chunks_dock_ == nullptr || inspector_dock_ == nullptr) {
    return;
  }
  chunks_dock_->setMinimumWidth(180);
  inspector_dock_->setMinimumWidth(260);
  chunks_dock_->setSizePolicy(QSizePolicy::Preferred,
                              QSizePolicy::Expanding);
  inspector_dock_->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Expanding);
  // The internal QMainWindow dock layout is not exposed as QSplitter
  // children.  Re-polish after the first layout pass so the style-sheet
  // separator extent is applied to the actual native separators as well.
  QMetaObject::invokeMethod(
      this,
      [this] {
        if (chunks_dock_ == nullptr || inspector_dock_ == nullptr) {
          return;
        }
        chunks_dock_->updateGeometry();
        inspector_dock_->updateGeometry();
        style()->unpolish(this);
        style()->polish(this);
        updateGeometry();
      },
      Qt::QueuedConnection);
}

void MainWindow::restoreWorkspace() {
  QSettings settings;
  const bool has_saved =
      settings.value(QStringLiteral("workspace/version")).toInt() == 1 &&
      settings.contains(QStringLiteral("workspace/geometry")) &&
      settings.contains(QStringLiteral("workspace/mainState")) &&
      settings.contains(QStringLiteral("workspace/splitterState"));
  if (!has_saved ||
      !restoreGeometry(settings.value(QStringLiteral("workspace/geometry"))
                           .toByteArray()) ||
      !restoreState(settings.value(QStringLiteral("workspace/mainState"))
                        .toByteArray()) ||
      !center_splitter_->restoreState(
          settings.value(QStringLiteral("workspace/splitterState"))
              .toByteArray())) {
    applyDefaultWorkspace();
    return;
  }
  if (settings.contains(QStringLiteral("workspace/chunkDetailSplitterState"))) {
    chunks_splitter_->restoreState(
        settings.value(QStringLiteral("workspace/chunkDetailSplitterState"))
            .toByteArray());
  }

  const int preview_index =
      settings.value(QStringLiteral("workspace/previewTab"), 0).toInt();
  const int inspector_index =
      settings.value(QStringLiteral("workspace/inspectorTab"), 0).toInt();
  if (preview_index < 0 || preview_index >= preview_tabs_->count() ||
      inspector_index < 0 || inspector_index >= inspector_tabs_->count()) {
    applyDefaultWorkspace();
    return;
  }
  preview_tabs_->setCurrentIndex(preview_index);
  inspector_tabs_->setCurrentIndex(inspector_index);
  const int image_page = settings.value(QStringLiteral("workspace/imagePage"), 0).toInt();
  const int scanline_page = settings.value(QStringLiteral("workspace/scanlinePage"), 0).toInt();
  const int compression_page = settings.value(QStringLiteral("workspace/compressionPage"), 0).toInt();
  if (image_page < 0 || image_page >= image_inspector_tabs_->count() ||
      scanline_page < 0 || scanline_page >= scanline_inspector_tabs_->count() ||
      compression_page < 0 || compression_page >= compression_inspector_tabs_->count()) {
    applyDefaultWorkspace();
    return;
  }
  image_inspector_tabs_->setCurrentIndex(image_page);
  scanline_inspector_tabs_->setCurrentIndex(scanline_page);
  compression_inspector_tabs_->setCurrentIndex(compression_page);

  const int base = settings.value(QStringLiteral("view/numericBase"), 0).toInt();
  const int source = settings.value(QStringLiteral("view/hexSource"), 0).toInt();
  if (base < 0 || base > 1 || source < 0 || source > 3) {
    applyDefaultWorkspace();
    return;
  }
  view_state_.numeric_base = base == 1
                                 ? pnga::ui::qt::NumericBase::kHexadecimal
                                 : pnga::ui::qt::NumericBase::kDecimal;
  view_state_.hex_source = static_cast<pnga::ui::qt::HexSource>(source);
  view_state_.hex_follow_pixel =
      settings.value(QStringLiteral("view/hexFollowPixel"), true).toBool();
  {
    const QSignalBlocker base_blocker(base_button_);
    const QSignalBlocker source_blocker(hex_source_combo_);
    const QSignalBlocker follow_blocker(hex_follow_check_);
    updateNumericBaseButton();
    hex_source_combo_->setCurrentIndex(source);
    hex_follow_check_->setChecked(view_state_.hex_follow_pixel);
  }
  inspector_->setNumericBase(base == 1);
  updateHexSource();
}

void MainWindow::saveWorkspace() const {
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 1);
  settings.setValue(QStringLiteral("workspace/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("workspace/mainState"), saveState());
  settings.setValue(QStringLiteral("workspace/splitterState"),
                    center_splitter_->saveState());
  settings.setValue(QStringLiteral("workspace/chunkDetailSplitterState"),
                    chunks_splitter_->saveState());
  settings.setValue(QStringLiteral("workspace/previewTab"),
                    preview_tabs_->currentIndex());
  settings.setValue(QStringLiteral("workspace/inspectorTab"),
                    inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("workspace/imagePage"),
                    image_inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("workspace/scanlinePage"),
                    scanline_inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("workspace/compressionPage"),
                    compression_inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("view/numericBase"),
                    static_cast<int>(view_state_.numeric_base));
  settings.setValue(QStringLiteral("view/hexSource"),
                    static_cast<int>(view_state_.hex_source));
  settings.setValue(QStringLiteral("view/hexFollowPixel"),
                    view_state_.hex_follow_pixel);
}

void MainWindow::refreshRecentFilesMenu() {
  if (recent_files_menu_ == nullptr) {
    return;
  }

  QSettings settings;
  const QStringList stored =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();
  QStringList valid;
  valid.reserve(kMaxRecentFiles);
  for (const QString& stored_path : stored) {
    if (valid.size() >= kMaxRecentFiles) {
      break;
    }
    if (stored_path.isEmpty()) {
      continue;
    }
    const QFileInfo info(stored_path);
    const QString path = info.absoluteFilePath();
    if (!info.exists() || !info.isFile() || valid.contains(path)) {
      continue;
    }
    valid.push_back(path);
  }
  if (valid != stored) {
    settings.setValue(QLatin1String(kRecentFilesSettingsKey), valid);
  }

  recent_files_menu_->clear();
  if (valid.isEmpty()) {
    QAction* empty = recent_files_menu_->addAction(
        QStringLiteral("No Recent Files"));
    empty->setObjectName(QStringLiteral("noRecentFiles"));
    empty->setEnabled(false);
    return;
  }

  for (const QString& path : valid) {
    const QFileInfo info(path);
    const QString label = info.fileName().isEmpty()
                              ? QDir::toNativeSeparators(path)
                              : info.fileName();
    QAction* action = recent_files_menu_->addAction(label);
    action->setData(path);
    action->setToolTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path] {
      openRecentFile(path);
    });
  }
}

void MainWindow::rememberLastOpenDirectory(const QString& path) {
  const QFileInfo info(path);
  const QString directory = info.absolutePath();
  if (!directory.isEmpty() && QDir(directory).exists()) {
    QSettings settings;
    settings.setValue(QLatin1String(kLastOpenDirectorySettingsKey), directory);
  }
}

QString MainWindow::lastOpenDirectory() const {
  QSettings settings;
  const QString directory =
      settings.value(QLatin1String(kLastOpenDirectorySettingsKey)).toString();
  return !directory.isEmpty() && QDir(directory).exists() ? directory
                                                           : QString();
}

void MainWindow::rememberOpenedFile(const QString& path) {
  const QFileInfo info(path);
  const QString normalized = info.absoluteFilePath();
  if (normalized.isEmpty()) {
    return;
  }

  QSettings settings;
  const QStringList stored =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();
  QStringList updated;
  updated.reserve(kMaxRecentFiles);
  updated.push_back(normalized);
  for (const QString& stored_path : stored) {
    if (updated.size() >= kMaxRecentFiles) {
      break;
    }
    if (stored_path.isEmpty()) {
      continue;
    }
    const QFileInfo stored_info(stored_path);
    const QString candidate = stored_info.absoluteFilePath();
    if (!stored_info.exists() || !stored_info.isFile() ||
        updated.contains(candidate)) {
      continue;
    }
    updated.push_back(candidate);
  }
  settings.setValue(QLatin1String(kRecentFilesSettingsKey), updated);
  rememberLastOpenDirectory(normalized);
  refreshRecentFilesMenu();
}

void MainWindow::openRecentFile(const QString& path) {
  if (openFile(path)) {
    return;
  }
  refreshRecentFilesMenu();
  QMessageBox::warning(
      this, QStringLiteral("PNG Analyzer"),
      QStringLiteral("Could not open recent file:\n%1").arg(path));
}

void MainWindow::updateHexSource() {
  if (source_ == nullptr) {
    hex_->setSource(nullptr);
    return;
  }
  const std::shared_ptr<const pnga::io::IByteSource> source = source_;
  if (view_state_.hex_source == pnga::ui::qt::HexSource::kIdatStream) {
    const pnga::png_format::VirtualIDATStream stream(index_);
    hex_->setSource(pnga::ui::qt::make_idat_hex_source(source, stream));
  } else if (view_state_.hex_source == pnga::ui::qt::HexSource::kInflated) {
    hex_->setSource(pnga::ui::qt::make_inflated_hex_source(stage_set_));
  } else if (view_state_.hex_source ==
             pnga::ui::qt::HexSource::kDefiltered) {
    hex_->setSource(pnga::ui::qt::make_defiltered_hex_source(stage_set_));
  } else {
    hex_->setSource(pnga::ui::qt::make_file_hex_source(source));
  }
  hex_->clearHighlight();
}

void MainWindow::resetLayout() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("workspace"));
  settings.clear();
  settings.endGroup();
  settings.beginGroup(QStringLiteral("view"));
  settings.clear();
  settings.endGroup();
  applyDefaultWorkspace();
}

void MainWindow::updateNumericBaseButton() {
  if (base_button_ == nullptr) {
    return;
  }
  const bool hexadecimal =
      view_state_.numeric_base == pnga::ui::qt::NumericBase::kHexadecimal;
  const QString target = hexadecimal ? QStringLiteral("DEC")
                                     : QStringLiteral("HEX");
  base_button_->setText(target);
  base_button_->setToolTip(QStringLiteral("Switch to %1").arg(target));
}

void MainWindow::publishLockedCoordinate() {
  const pnga::trace_model::ImageCoordinate coordinate{
      0, 0, 0, static_cast<std::uint64_t>(x_spin_->value()),
      static_cast<std::uint64_t>(y_spin_->value())};
  if (!view_state_.set_locked(coordinate)) {
    return;
  }
  image_view_->setLockedPixel(
      QPoint(static_cast<int>(coordinate.x), static_cast<int>(coordinate.y)));
  pixel_view_->setCenter(coordinate.x, coordinate.y);
  filter_map_view_->setCoordinate(coordinate.x, coordinate.y);
  filtered_view_->setCoordinate(coordinate.x, coordinate.y);
  defiltered_view_->setCoordinate(coordinate.x, coordinate.y);
  inspector_->onPixelSelected(coordinate.x, coordinate.y);
  pnga::trace_model::Selection update;
  update.image = coordinate;
  update.stage = pnga::trace_model::Stage::kDelivered;
  bus_->publishMerged(kImagePanelOrigin, generation_, update);
}

void MainWindow::clearLockedCoordinate() {
  view_state_.clear_locked();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  if (view_state_.hover.has_value()) {
    setPixelStatus(static_cast<int>(view_state_.hover->x),
                   static_cast<int>(view_state_.hover->y));
  } else {
    restorePixelStatus();
  }
  pnga::trace_model::Selection current = bus_->current();
  current.image.reset();
  if (current.stage == pnga::trace_model::Stage::kDelivered) {
    current.stage = pnga::trace_model::Stage::kUnknown;
  }
  bus_->publish(kImagePanelOrigin, generation_, current);
}

void MainWindow::nudgeLockedCoordinate(int dx, int dy) {
  if (!view_state_.locked.has_value() || image_view_->image().isNull()) {
    return;
  }
  const QImage image = image_view_->image();
  std::uint64_t x = view_state_.locked->x;
  std::uint64_t y = view_state_.locked->y;
  if (dx < 0) {
    if (x == 0) {
      return;
    }
    --x;
  } else if (dx > 0) {
    if (x >= static_cast<std::uint64_t>(image.width() - 1)) {
      return;
    }
    ++x;
  }
  if (dy < 0) {
    if (y == 0) {
      return;
    }
    --y;
  } else if (dy > 0) {
    if (y >= static_cast<std::uint64_t>(image.height() - 1)) {
      return;
    }
    ++y;
  }
  onPixelSelected(static_cast<int>(x), static_cast<int>(y));
}

void MainWindow::paintEvent(QPaintEvent* event) {
  QMainWindow::paintEvent(event);
  if (event == nullptr) {
    return;
  }

  QPainter painter(this);
  painter.setClipRegion(event->region());
  const int extent = style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent,
                                          nullptr, this);
  if (extent <= 0) {
    return;
  }

  const QColor dot_color(160, 160, 160, 145);
  const auto drawDots = [&](QDockWidget* dock) {
    if (dock == nullptr || !dock->isVisible() || dock->isFloating()) {
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(dock);
    if (area != Qt::LeftDockWidgetArea && area != Qt::RightDockWidgetArea) {
      return;
    }

    const QRect dock_rect = dock->geometry();
    const int separator_x = area == Qt::LeftDockWidgetArea
                                ? dock_rect.right() + 1
                                : dock_rect.left() - extent;
    const QRect handle(separator_x, dock_rect.top(), extent, dock_rect.height());
    if (!handle.intersects(rect())) {
      return;
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(dot_color);
    const QPointF center(handle.center());
    constexpr qreal radius = 1.35;
    constexpr qreal spacing = 4.0;
    for (int index = -1; index <= 1; ++index) {
      painter.drawEllipse(center + QPointF(0.0, index * spacing), radius,
                          radius);
    }
    painter.restore();
  };

  drawDots(chunks_dock_);
  drawDots(inspector_dock_);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == x_spin_ || watched == y_spin_ || watched == lock_check_ ||
       watched == base_button_ || watched == hex_follow_check_ ||
       watched == preview_tabs_ || watched == inspector_tabs_) &&
      event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Escape) {
      clearLockedCoordinate();
      key_event->accept();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveWorkspace();
  QMainWindow::closeEvent(event);
}

void MainWindow::openQueryCoordinator(
    const pnga::png_reconstruction::ImageHeader& header) {
  query_.reset();
  query_ = std::make_unique<pnga::analysis_engine::QueryCoordinator>(
      /*worker_count=*/2, /*replay_budget_bytes=*/1ull << 26);
  const std::shared_ptr<const pnga::io::IByteSource> shared =
      std::shared_ptr<const pnga::io::IByteSource>(source_);
  if (!query_->open(shared, header, /*anchor_interval_bytes=*/16384)) {
    query_.reset();
    return;
  }
  // Bridge the worker-thread status callback onto the GUI thread.
  query_->setStatusCallback(
      [bridge = query_bridge_](std::uint64_t row, pnga::analysis_engine::QueryStatus status) {
        QMetaObject::invokeMethod(
            bridge, [bridge, row, status] {
              emit bridge->rowStatus(row, static_cast<int>(status));
            },
            Qt::QueuedConnection);
      });
}

void MainWindow::startStageAnalysis() {
  if (stage_worker_ != nullptr) {
    stage_worker_ = nullptr;  // in-flight stage result becomes stale
  }
  auto* worker = new StageWorker(generation_, source_, this);
  stage_worker_ = worker;
  connect(worker, &StageWorker::stageDone, this, &MainWindow::onStageDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void MainWindow::startValidation() {
  if (validation_worker_ != nullptr) {
    validation_worker_ = nullptr;
  }
  auto* worker = new ValidationWorker(generation_, source_, index_, this);
  validation_worker_ = worker;
  connect(worker, &ValidationWorker::validationDone, this,
          &MainWindow::onValidationDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void MainWindow::onStageDone(std::uint64_t generation) {
  if (generation != generation_ || stage_worker_ == nullptr) {
    return;  // stale stage analysis; never overwrite the current document
  }
  const auto stage = stage_worker_->result();
  stage_set_ = stage;
  const auto header = stage->header;
  inspector_->setStageSet(stage);
  pixel_view_->setStageSet(stage);
  filter_map_view_->setStageSet(stage);
  filtered_view_->setStageSet(stage);
  defiltered_view_->setStageSet(stage);
  updateHexSource();
  stage_worker_ = nullptr;
  openQueryCoordinator(header);
}

void MainWindow::onValidationDone(std::uint64_t generation) {
  if (generation != generation_ || validation_worker_ == nullptr) {
    return;
  }
  validation_report_ = validation_worker_->result();
  if (validation_report_.issues.empty()) {
    validation_label_->setText(QStringLiteral("Validation: OK"));
    validation_label_->setToolTip(QStringLiteral("No validation issues"));
  } else {
    const auto& first = validation_report_.issues.front();
    validation_label_->setText(
        QStringLiteral("Validation: %1 issue(s), %2 @ %3")
            .arg(static_cast<qulonglong>(validation_report_.issues.size()))
            .arg(QString::fromStdString(first.rule_id))
            .arg(static_cast<qulonglong>(first.offset)));
    QString tooltip;
    for (const auto& issue : validation_report_.issues) {
      if (!tooltip.isEmpty()) {
        tooltip += QStringLiteral("\n");
      }
      tooltip += QStringLiteral("%1 @ %2: %3")
                     .arg(QString::fromStdString(issue.rule_id))
                     .arg(static_cast<qulonglong>(issue.offset))
                     .arg(QString::fromStdString(issue.message));
    }
    validation_label_->setToolTip(tooltip);
  }
  validation_worker_ = nullptr;
}

void MainWindow::onRowQueryStatus(std::uint64_t row, int status) {
  // Worker-thread callback bridged to the GUI thread; show the latest status.
  inspector_->setRowQueryStatus(QLatin1String(
      pnga::analysis_engine::query_status_text(
          static_cast<pnga::analysis_engine::QueryStatus>(status))));
  (void)row;
}

bool MainWindow::openFile(const QString& path) {
  std::unique_ptr<pnga::io::IByteSource> opened;
  const std::error_code ec = pnga::io::open_mapped_file(
      std::filesystem::path(path.toStdString()), opened);
  if (ec) {
    return false;
  }
  // Shared ownership so an in-flight worker keeps its source alive even when a
  // newer file replaces source_ (virtual dtor makes this safe).
  auto source =
      std::shared_ptr<pnga::io::IByteSource>(opened.release());
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  rememberOpenedFile(path);
  if (!current_file_path_.isEmpty()) {
    const QString previous_suffix =
        QStringLiteral(" — %1").arg(current_file_path_);
    if (windowTitle().endsWith(previous_suffix)) {
      setWindowTitle(windowTitle().left(windowTitle().size() -
                                       previous_suffix.size()));
    }
  }
  setWindowTitle(QStringLiteral("%1 — %2").arg(windowTitle(), absolute_path));
  current_file_path_ = absolute_path;
  default_pixel_status_ = QStringLiteral("Loading image…");
  pixel_label_->setText(default_pixel_status_);
  source_ = source;
  index_ = pnga::png_format::index_chunks(*source_);
  ++generation_;
  bus_->setDocumentGeneration(generation_);
  view_state_.set_document_generation(generation_);
  stage_set_.reset();
  pixel_view_->clear();
  filter_map_view_->clear();
  filtered_view_->clear();
  defiltered_view_->clear();
  image_view_->clearHoverPixel();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  resetDocument();
  validation_report_ = {};
  validation_label_->setText(QStringLiteral("Validation: checking…"));
  validation_label_->setToolTip(QString());
  startValidation();
  startDecode();
  startStageAnalysis();
  return true;
}

void MainWindow::onOpenTriggered() {
  // Development builds may be launched directly from inside the .app bundle
  // and do not carry the production bundle metadata used by NSOpenPanel. Use
  // Qt's dialog in that case so File -> Open remains visible and modal on
  // macOS as well as on the other desktop platforms.
  raise();
  activateWindow();
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open PNG"), lastOpenDirectory(),
      QStringLiteral("PNG files (*.png);;All files (*)"), nullptr,
      QFileDialog::DontUseNativeDialog);
  if (path.isEmpty()) {
    return;
  }
  // Preserve the dialog location even when the selected file turns out to be
  // unreadable, so the next Open action still starts in the same directory.
  rememberLastOpenDirectory(path);
  if (!openFile(path)) {
    QMessageBox::warning(this, QStringLiteral("PNG Analyzer"),
                         QStringLiteral("Could not open file:\n%1").arg(path));
  }
}

void MainWindow::resetDocument() {
  ++chunk_selection_serial_;
  if (chunk_detail_ != nullptr) {
    chunk_detail_->clear();
  }
  if (model_ != nullptr) {
    delete model_;
  }
  model_ = new pnga::ui::qt::ChunkModel(&index_, this);
  tree_->setModel(model_);
  // setModel() replaces the selection model; reconnect to the new one.
  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onChunkSelectionChanged);
  updateHexSource();

  if (model_->rowCount() > 0) {
    tree_->selectionModel()->setCurrentIndex(
        model_->index(0, 0),
        QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  }
}

void MainWindow::startDecode() {
  if (decode_worker_ != nullptr) {
    // A previous decode may still be running; its result will be ignored
    // because its generation is stale. Drop the reference now.
    decode_worker_ = nullptr;
  }
  auto* worker = new DecodeWorker(generation_, source_, this);
  decode_worker_ = worker;
  connect(worker, &DecodeWorker::decodeDone, this, &MainWindow::onDecodeDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void MainWindow::onDecodeDone(std::uint64_t generation) {
  if (generation != generation_ || decode_worker_ == nullptr) {
    return;  // stale decode; never overwrite the current document's image
  }
  const auto& result = decode_worker_->result();
  if (!result.success) {
    image_view_->setImage(QImage());
    default_pixel_status_ = QStringLiteral("decode failed: %1")
                                .arg(QString::fromStdString(result.error));
    pixel_label_->setText(default_pixel_status_);
    decode_worker_ = nullptr;
    return;
  }
  const auto& img = result.image;
  QImage qimage(static_cast<int>(img.width), static_cast<int>(img.height),
                QImage::Format_RGBA8888);
  std::memcpy(qimage.bits(), img.rgba.data(), img.rgba.size());
  image_view_->setImage(qimage);
  // Feed the delivered RGBA to the stage inspector's Delivered stage.
  inspector_->setDeliveredPixels(img.width, img.height, img.rgba);
  default_pixel_status_ =
      QStringLiteral("%1 x %2  (bit depth %3, color type %4)")
          .arg(img.width)
          .arg(img.height)
          .arg(img.source_bit_depth)
          .arg(img.source_color_type);
  pixel_label_->setText(default_pixel_status_);
  decode_worker_ = nullptr;
}

void MainWindow::onChunkSelectionChanged(const QModelIndex& current,
                                         const QModelIndex& /*previous*/) {
  const std::uint64_t selection_serial = ++chunk_selection_serial_;
  if (!current.isValid()) {
    hex_->clearHighlight();
    if (chunk_detail_ != nullptr) {
      chunk_detail_->clear();
    }
    return;
  }
  const auto& node = model_->chunkAt(current.row());

  if (chunk_detail_ != nullptr && source_ != nullptr) {
    chunk_detail_->setLoading();
    auto* detail_worker = new ChunkDetailWorker(
        generation_, selection_serial, source_, node, this);
    chunk_detail_worker_ = detail_worker;
    connect(detail_worker, &ChunkDetailWorker::detailDone, this,
            &MainWindow::onChunkDetailDone);
    connect(detail_worker, &QThread::finished, detail_worker,
            &QObject::deleteLater);
    connect(detail_worker, &QThread::finished, this,
            [this, detail_worker] {
              if (chunk_detail_worker_ == detail_worker) {
                chunk_detail_worker_ = nullptr;
              }
            });
    detail_worker->start();
  }

  std::vector<pnga::ui::qt::HexHighlightSpan> spans;
  spans.push_back({node.header_offset, kHeaderSpanLength,
                   QColor(0x9E, 0x9E, 0x9E)});  // header: gray
  spans.push_back({node.data_offset, node.data_length,
                   QColor(0x42, 0xA5, 0xF5)});  // data: blue
  spans.push_back({node.crc_offset, kCrcSpanLength,
                   QColor(0x66, 0xBB, 0x6A)});  // CRC: green
  hex_->setHighlight(std::move(spans));

  hex_->navigateTo(node.header_offset);

  // Publish the canonical selection through the bus (single controller).
  pnga::trace_model::Selection sel;
  sel.node = static_cast<pnga::trace_model::NodeId>(current.row());
  sel.physical_spans = {
      pnga::trace_model::BitSpan{node.header_offset, kHeaderSpanLength},
      pnga::trace_model::BitSpan{node.data_offset, node.data_length},
      pnga::trace_model::BitSpan{node.crc_offset, kCrcSpanLength}};
  sel.stage = pnga::trace_model::Stage::kChunk;
  bus_->publishMerged(kChunkPanelOrigin, generation_, sel);
}

void MainWindow::onChunkDetailDone(std::uint64_t generation,
                                   std::uint64_t selection_serial) {
  if (generation != generation_ ||
      selection_serial != chunk_selection_serial_ ||
      chunk_detail_worker_ == nullptr || chunk_detail_ == nullptr) {
    return;
  }
  chunk_detail_->setDetail(chunk_detail_worker_->result());
}

void MainWindow::setPixelStatus(int x, int y) {
  const auto rgba = image_view_->rgbaAt(x, y);
  if (!rgba.has_value()) {
    restorePixelStatus();
    return;
  }
  pixel_label_->setText(
      QStringLiteral("pixel (%1, %2) RGBA(%3, %4, %5, %6)")
          .arg(x)
          .arg(y)
          .arg((*rgba)[0])
          .arg((*rgba)[1])
          .arg((*rgba)[2])
          .arg((*rgba)[3]));
}

void MainWindow::restorePixelStatus() {
  if (view_state_.locked.has_value()) {
    setPixelStatus(static_cast<int>(view_state_.locked->x),
                   static_cast<int>(view_state_.locked->y));
    return;
  }
  pixel_label_->setText(default_pixel_status_);
}

void MainWindow::onPixelSelected(int x, int y) {
  {
    const QSignalBlocker x_blocker(x_spin_);
    const QSignalBlocker y_blocker(y_spin_);
    const QSignalBlocker lock_blocker(lock_check_);
    x_spin_->setValue(x);
    y_spin_->setValue(y);
    lock_check_->setChecked(true);
  }
  inspector_->onPixelSelected(static_cast<std::uint64_t>(x),
                              static_cast<std::uint64_t>(y));
  if (query_ != nullptr && query_->has_index() &&
      stage_worker_ == nullptr) {
    // Map the clicked pixel to its stream row and issue a selection-priority
    // replay if the row's data is not materialized yet.
    const auto& layout = query_->anchors().layout;
    const auto row = pnga::analysis_engine::stream_row_for_pixel(
        layout, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y));
    if (row.has_value()) {
      const auto result = query_->query_scanline(
          *row, pnga::analysis_engine::JobPriority::kSelection);
      inspector_->setRowQueryStatus(QLatin1String(
          pnga::analysis_engine::query_status_text(result.status)));
    }
  }
  pnga::trace_model::Selection sel;
  sel.image = pnga::trace_model::ImageCoordinate{
      0, 0, 0, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y)};
  sel.stage = pnga::trace_model::Stage::kDelivered;
  view_state_.set_locked(*sel.image);
  image_view_->setLockedPixel(QPoint(x, y));
  pixel_view_->setCenter(static_cast<std::uint64_t>(x),
                         static_cast<std::uint64_t>(y));
  filter_map_view_->setCoordinate(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
  filtered_view_->setCoordinate(static_cast<std::uint64_t>(x),
                                static_cast<std::uint64_t>(y));
  defiltered_view_->setCoordinate(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
  bus_->publishMerged(kImagePanelOrigin, generation_, sel);
  setPixelStatus(x, y);
}
