// WP-104/204 MainWindow: docks, menu, file open, chunk->hex selection and the
// background reference decode into the delivered image view.

#include "main_window.h"

#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <QSettings>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;

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

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("PNG Analyzer"));

  center_splitter_ = new QSplitter(Qt::Vertical, this);
  center_splitter_->setObjectName(QStringLiteral("previewHexSplitter"));
  center_splitter_->setChildrenCollapsible(false);

  preview_tabs_ = new QTabWidget(center_splitter_);
  preview_tabs_->setObjectName(QStringLiteral("previewTabs"));
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
  center_splitter_->addWidget(preview_tabs_);
  center_splitter_->addWidget(hex_);
  center_splitter_->setStretchFactor(0, 3);
  center_splitter_->setStretchFactor(1, 2);
  setCentralWidget(center_splitter_);

  chunks_dock_ = new QDockWidget(QStringLiteral("Chunks"), this);
  chunks_dock_->setObjectName(QStringLiteral("chunksDock"));
  chunks_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  chunks_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
  tree_ = new QTreeView(chunks_dock_);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setUniformRowHeights(true);
  tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  chunks_dock_->setWidget(tree_);
  addDockWidget(Qt::LeftDockWidgetArea, chunks_dock_);

  bus_ = new pnga::ui::qt::SelectionBus(this);

  inspector_dock_ = new QDockWidget(QStringLiteral("Inspector"), this);
  inspector_dock_->setObjectName(QStringLiteral("inspectorDock"));
  inspector_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  inspector_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);
  auto* inspector_container = new QWidget(inspector_dock_);
  auto* inspector_layout = new QVBoxLayout(inspector_container);
  inspector_layout->setContentsMargins(6, 6, 6, 6);
  auto* coordinate_bar = new QWidget(inspector_container);
  coordinate_bar->setObjectName(QStringLiteral("coordinateToolbar"));
  auto* coordinate_layout = new QHBoxLayout(coordinate_bar);
  coordinate_layout->setContentsMargins(0, 0, 0, 0);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("X"), coordinate_bar));
  x_spin_ = new QSpinBox(coordinate_bar);
  x_spin_->setObjectName(QStringLiteral("xCoordinate"));
  x_spin_->setRange(0, std::numeric_limits<int>::max());
  coordinate_layout->addWidget(x_spin_);
  coordinate_layout->addWidget(new QLabel(QStringLiteral("Y"), coordinate_bar));
  y_spin_ = new QSpinBox(coordinate_bar);
  y_spin_->setObjectName(QStringLiteral("yCoordinate"));
  y_spin_->setRange(0, std::numeric_limits<int>::max());
  coordinate_layout->addWidget(y_spin_);
  lock_check_ = new QCheckBox(QStringLiteral("Lock"), coordinate_bar);
  lock_check_->setObjectName(QStringLiteral("lockCoordinate"));
  coordinate_layout->addWidget(lock_check_);
  base_combo_ = new QComboBox(coordinate_bar);
  base_combo_->setObjectName(QStringLiteral("numericBase"));
  base_combo_->addItem(QStringLiteral("DEC"));
  base_combo_->addItem(QStringLiteral("HEX"));
  coordinate_layout->addWidget(base_combo_);
  hex_source_combo_ = new QComboBox(coordinate_bar);
  hex_source_combo_->setObjectName(QStringLiteral("hexSource"));
  hex_source_combo_->addItem(QStringLiteral("File"));
  hex_source_combo_->addItem(QStringLiteral("IDAT Stream"));
  hex_source_combo_->addItem(QStringLiteral("Inflated"));
  hex_source_combo_->addItem(QStringLiteral("Defiltered"));
  coordinate_layout->addWidget(hex_source_combo_);
  hex_follow_check_ = new QCheckBox(QStringLiteral("Hex follows pixel"),
                                    coordinate_bar);
  hex_follow_check_->setObjectName(QStringLiteral("hexFollowPixel"));
  coordinate_layout->addWidget(hex_follow_check_);
  coordinate_layout->addStretch(1);
  inspector_layout->addWidget(coordinate_bar);

  inspector_tabs_ = new QTabWidget(inspector_container);
  inspector_tabs_->setObjectName(QStringLiteral("inspectorTabs"));
  inspector_tabs_->setUsesScrollButtons(true);
  inspector_ = new pnga::ui::qt::StageInspector(inspector_tabs_);
  inspector_tabs_->addTab(inspector_, QStringLiteral("Reconstruct"));
  const auto addInspectorPlaceholder = [this](const QString& title) {
    auto* label = new QLabel(QStringLiteral("Not available for current selection"),
                             inspector_tabs_);
    label->setAlignment(Qt::AlignCenter);
    inspector_tabs_->addTab(label, title);
  };
  addInspectorPlaceholder(QStringLiteral("Pixel"));
  addInspectorPlaceholder(QStringLiteral("Scanline"));
  addInspectorPlaceholder(QStringLiteral("Source"));
  addInspectorPlaceholder(QStringLiteral("Format Context"));
  block_inspector_ = new pnga::ui::qt::BlockInspector(inspector_tabs_);
  inspector_tabs_->addTab(block_inspector_, QStringLiteral("DEFLATE / Block"));
  huffman_inspector_ = new pnga::ui::qt::HuffmanInspector(inspector_tabs_);
  inspector_tabs_->addTab(huffman_inspector_,
                          QStringLiteral("DEFLATE / Huffman Tables"));
  inspector_layout->addWidget(inspector_tabs_, 1);
  inspector_dock_->setWidget(inspector_container);
  addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

  pixel_label_ = new QLabel(QStringLiteral("No image"), this);
  statusBar()->addWidget(pixel_label_);

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);

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
          });
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
          this, [this] { view_state_.clear_hover(); });
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested, this,
          &MainWindow::nudgeLockedCoordinate);
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::selectionCancelled, this,
          &MainWindow::clearLockedCoordinate);
  x_spin_->installEventFilter(this);
  y_spin_->installEventFilter(this);
  lock_check_->installEventFilter(this);
  base_combo_->installEventFilter(this);
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
  connect(base_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            view_state_.numeric_base = index == 1
                                           ? pnga::ui::qt::NumericBase::kHexadecimal
                                           : pnga::ui::qt::NumericBase::kDecimal;
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
}

void MainWindow::applyDefaultWorkspace() {
  const auto preserved_locked = view_state_.locked;
  const auto preserved_hover = view_state_.hover;
  resize(1200, 760);
  center_splitter_->setSizes({456, 304});
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  chunks_dock_->show();
  inspector_dock_->show();
  resizeDocks({chunks_dock_, inspector_dock_}, {260, 360},
              Qt::Horizontal);

  view_state_.hex_source = pnga::ui::qt::HexSource::kFile;
  view_state_.numeric_base = pnga::ui::qt::NumericBase::kDecimal;
  view_state_.hex_follow_pixel = true;
  view_state_.locked = preserved_locked;
  view_state_.hover = preserved_hover;
  {
    const QSignalBlocker base_blocker(base_combo_);
    const QSignalBlocker source_blocker(hex_source_combo_);
    const QSignalBlocker follow_blocker(hex_follow_check_);
    const QSignalBlocker lock_blocker(lock_check_);
    base_combo_->setCurrentIndex(0);
    hex_source_combo_->setCurrentIndex(0);
    hex_follow_check_->setChecked(true);
    lock_check_->setChecked(preserved_locked.has_value());
    if (preserved_locked.has_value()) {
      x_spin_->setValue(static_cast<int>(preserved_locked->x));
      y_spin_->setValue(static_cast<int>(preserved_locked->y));
    }
  }
  updateHexSource();
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
    const QSignalBlocker base_blocker(base_combo_);
    const QSignalBlocker source_blocker(hex_source_combo_);
    const QSignalBlocker follow_blocker(hex_follow_check_);
    base_combo_->setCurrentIndex(base);
    hex_source_combo_->setCurrentIndex(source);
    hex_follow_check_->setChecked(view_state_.hex_follow_pixel);
  }
  updateHexSource();
}

void MainWindow::saveWorkspace() const {
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 1);
  settings.setValue(QStringLiteral("workspace/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("workspace/mainState"), saveState());
  settings.setValue(QStringLiteral("workspace/splitterState"),
                    center_splitter_->saveState());
  settings.setValue(QStringLiteral("workspace/previewTab"),
                    preview_tabs_->currentIndex());
  settings.setValue(QStringLiteral("workspace/inspectorTab"),
                    inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("view/numericBase"),
                    static_cast<int>(view_state_.numeric_base));
  settings.setValue(QStringLiteral("view/hexSource"),
                    static_cast<int>(view_state_.hex_source));
  settings.setValue(QStringLiteral("view/hexFollowPixel"),
                    view_state_.hex_follow_pixel);
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == x_spin_ || watched == y_spin_ || watched == lock_check_ ||
       watched == base_combo_ || watched == hex_follow_check_ ||
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
  startDecode();
  startStageAnalysis();
  return true;
}

void MainWindow::onOpenTriggered() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open PNG"), QString(),
      QStringLiteral("PNG files (*.png);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  if (!openFile(path)) {
    QMessageBox::warning(this, QStringLiteral("PNG Analyzer"),
                         QStringLiteral("Could not open file:\n%1").arg(path));
  }
}

void MainWindow::resetDocument() {
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
    pixel_label_->setText(QStringLiteral("decode failed: %1")
                              .arg(QString::fromStdString(result.error)));
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
  pixel_label_->setText(QStringLiteral("%1 x %2  (bit depth %3, color type %4)")
                            .arg(img.width)
                            .arg(img.height)
                            .arg(img.source_bit_depth)
                            .arg(img.source_color_type));
  decode_worker_ = nullptr;
}

void MainWindow::onChunkSelectionChanged(const QModelIndex& current,
                                         const QModelIndex& /*previous*/) {
  if (!current.isValid()) {
    hex_->clearHighlight();
    return;
  }
  const auto& node = model_->chunkAt(current.row());

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
  const auto rgba = image_view_->rgbaAt(x, y);
  if (rgba.has_value()) {
    pixel_label_->setText(
        QStringLiteral("pixel (%1, %2) RGBA(%3, %4, %5, %6)")
            .arg(x)
            .arg(y)
            .arg((*rgba)[0])
            .arg((*rgba)[1])
            .arg((*rgba)[2])
            .arg((*rgba)[3]));
  }
}
