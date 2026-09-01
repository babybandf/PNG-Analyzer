// WP-104/204 MainWindow: docks, menu, file open, chunk->hex selection and the
// background reference decode into the delivered image view.

#include "main_window.h"

#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_detail_panel.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_context.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/hex_data_source.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>
#include <pnga/ui/qt/stage_preview_view.h>
#include <pnga/ui/qt/trace_inspector_binding.h>
#include <pnga/analysis-engine/trace_inspector_state.h>
#include <pnga/analysis-engine/trace_orchestrator.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QMetaObject>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPair>
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
#include <QVector>
#include <QUrl>
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
#include <utility>
#include <vector>

namespace {

bool hasLocalPngUrl(const QMimeData* mime_data) {
  if (mime_data == nullptr || !mime_data->hasUrls()) {
    return false;
  }
  for (const QUrl& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QFileInfo file_info(url.toLocalFile());
    if (file_info.isFile() &&
        file_info.suffix().compare(QStringLiteral("png"),
                                   Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent,
                       pnga::ui::qt::ApplicationTheme* theme)
    : QMainWindow(parent), theme_(theme) {
  widgets_ = buildMainWindowUi(*this, theme);
  center_splitter_ = widgets_.center_splitter;
  preview_tabs_ = widgets_.preview_tabs;
  image_view_ = widgets_.image_view;
  pixel_view_ = widgets_.pixel_view;
  filtered_view_ = widgets_.filtered_view;
  defiltered_view_ = widgets_.defiltered_view;
  hex_panel_ = widgets_.hex_panel;
  hex_source_tabs_ = widgets_.hex_source_tabs;
  hex_ = widgets_.hex;
  chunks_dock_ = widgets_.chunks_dock;
  chunks_splitter_ = widgets_.chunks_splitter;
  tree_ = widgets_.tree;
  chunk_detail_ = widgets_.chunk_detail;
  bus_ = widgets_.bus;
  inspector_dock_ = widgets_.inspector_dock;
  x_spin_ = widgets_.x_spin;
  y_spin_ = widgets_.y_spin;
  lock_check_ = widgets_.lock_check;
  base_button_ = widgets_.base_button;
  inspector_tabs_ = widgets_.inspector_tabs;
  inspector_ = widgets_.inspector;
  compression_inspector_tabs_ = widgets_.compression_inspector_tabs;
  block_inspector_ = widgets_.block_inspector;
  huffman_inspector_ = widgets_.huffman_inspector;
  decode_trace_inspector_ = widgets_.decode_trace_inspector;
  compression_context_ = widgets_.compression_context;
  trace_binding_ = widgets_.trace_binding;
  pixel_label_ = widgets_.pixel_label;
  validation_label_ = widgets_.validation_label;
  close_action_ = widgets_.close_action;
  recent_files_menu_ = widgets_.recent_files_menu;

  workspace_ = std::make_unique<WorkspaceController>(
      *this, widgets_,
      [this](const QString& path) { openRecentFile(path); });
  session_ = std::make_unique<DocumentSession>(this);
  connect(session_.get(), &DocumentSession::decodePublished, this,
          &MainWindow::onDecodeDone);
  connect(session_.get(), &DocumentSession::stagesPublished, this,
          &MainWindow::onStageDone);
  connect(session_.get(), &DocumentSession::validationPublished, this,
          &MainWindow::onValidationDone);
  connect(session_.get(), &DocumentSession::chunkDetailPublished, this,
          &MainWindow::onChunkDetailDone);
  connect(session_.get(), &DocumentSession::rowQueryStatus, this,
          &MainWindow::onRowQueryStatus);
  selection_ = std::make_unique<SelectionNavigationController>(
      widgets_,
      SelectionNavigationCallbacks{
          [this](const pnga::trace_model::ImageCoordinate& coordinate) {
            trace_controller_->requestFor(coordinate);
          },
          [this](std::uint64_t row) {
            // Selection-priority replay for the freshly committed pixel; the
            // controller only forwards rows while a query index exists.
            pnga::analysis_engine::QueryCoordinator* query =
                session_->queryCoordinator();
            const auto result =
                query->query_scanline(row,
                                      pnga::analysis_engine::JobPriority::
                                          kSelection);
            inspector_->setRowQueryStatus(QLatin1String(
                pnga::analysis_engine::query_status_text(result.status)));
          },
          [this](const pnga::png_format::ChunkNode& node,
                 std::uint64_t selection_serial) {
            session_->requestChunkDetail(node, selection_serial);
          }},
      this, &workspace_->viewState());
  trace_controller_ = std::make_unique<TraceController>(widgets_, this);
  connect(trace_controller_.get(), &TraceController::hexSourceRequested,
          selection_.get(), &SelectionNavigationController::setHexSource);
  connect(widgets_.open_action, &QAction::triggered, this,
          &MainWindow::onOpenTriggered);
  connect(close_action_, &QAction::triggered, this,
          &MainWindow::onCloseTriggered);
  connect(recent_files_menu_, &QMenu::aboutToShow, this,
          [this] { workspace_->refreshRecentFilesMenu(); });
  workspace_->refreshRecentFilesMenu();
  connect(widgets_.exit_action, &QAction::triggered, this,
          [this] { close(); });

  connect(widgets_.reset_layout_action, &QAction::triggered, this,
          &MainWindow::resetLayout);
  connect(widgets_.show_hex_view_action, &QAction::toggled, this,
          [this](bool visible) { hex_panel_->setVisible(visible); });
  if (theme_ != nullptr) {
    const auto connectThemeAction = [this](QAction* action) {
      const auto mode = static_cast<pnga::ui::qt::ApplicationTheme::ThemeMode>(
          action->data().toInt());
      connect(action, &QAction::triggered, this, [this, mode] {
        theme_->setMode(mode);
      });
    };
    connectThemeAction(widgets_.theme_system_action);
    connectThemeAction(widgets_.theme_light_action);
    connectThemeAction(widgets_.theme_dark_action);
    const auto updateThemeActions = [this] {
      widgets_.theme_system_action->setChecked(
          theme_->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
      widgets_.theme_light_action->setChecked(
          theme_->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
      widgets_.theme_dark_action->setChecked(
          theme_->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
    };
    connect(theme_, &pnga::ui::qt::ApplicationTheme::themeChanged, this,
            updateThemeActions);
    updateThemeActions();
  }

  QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  helpMenu->addAction(QStringLiteral("About"), this, [this] {
    pnga::ui::qt::AboutDialog dialog(
        pnga::ui::qt::default_about_content(), this);
    dialog.exec();
  });

  // Initial empty model so the selection-model connection is valid from the
  // start; the controller reconnects after each real setModel.
  selection_->replaceChunkModel(&session_->index());

  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelSelected,
          selection_.get(), &SelectionNavigationController::onPixelSelected);
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHovered,
          selection_.get(), &SelectionNavigationController::onPixelHovered);
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
          selection_.get(), &SelectionNavigationController::onPixelHoverLeft);
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested,
          selection_.get(), &SelectionNavigationController::nudgeLockedCoordinate);
  connect(image_view_,
          &pnga::ui::qt::DeliveredImageView::selectionCancelled,
          selection_.get(),
          &SelectionNavigationController::clearLockedCoordinate);
  x_spin_->installEventFilter(this);
  y_spin_->installEventFilter(this);
  lock_check_->installEventFilter(this);
  base_button_->installEventFilter(this);
  preview_tabs_->installEventFilter(this);
  hex_source_tabs_->installEventFilter(this);
  inspector_tabs_->installEventFilter(this);

  connect(x_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (lock_check_->isChecked()) {
              selection_->publishLockedCoordinate();
            }
          });
  connect(y_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (lock_check_->isChecked()) {
              selection_->publishLockedCoordinate();
            }
          });
  connect(lock_check_, &QCheckBox::toggled, this, [this](bool locked) {
    if (locked) {
      selection_->publishLockedCoordinate();
    } else {
      selection_->clearLockedCoordinate();
    }
  });
  connect(base_button_, &QPushButton::clicked, selection_.get(),
          &SelectionNavigationController::toggleNumericBase);
  connect(hex_source_tabs_, &pnga::ui::qt::HexSourceTabBar::sourceChanged,
          selection_.get(),
          &SelectionNavigationController::onHexSourceTabChanged);

  resize(1200, 760);
  workspace_->applyDefaults();
  workspace_->restore();
  selection_->refreshHexSource();
  workspace_->configureDockInteraction();
}

MainWindow::~MainWindow() = default;


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
       watched == base_button_ || watched == preview_tabs_ ||
       watched == hex_source_tabs_ || watched == inspector_tabs_) &&
      event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Escape) {
      selection_->clearLockedCoordinate();
      key_event->accept();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  workspace_->save();
  QMainWindow::closeEvent(event);
}

void MainWindow::resetLayout() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("workspace"));
  settings.clear();
  settings.endGroup();
  settings.beginGroup(QStringLiteral("view"));
  settings.clear();
  settings.endGroup();
  workspace_->applyDefaults();
  selection_->refreshHexSource();
}

void MainWindow::openRecentFile(const QString& path) {
  if (openFile(path)) {
    return;
  }
  workspace_->refreshRecentFilesMenu();
  QMessageBox::warning(
      this, QStringLiteral("PNG Analyzer"),
      QStringLiteral("Could not open recent file:\n%1").arg(path));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (hasLocalPngUrl(event->mimeData())) {
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (hasLocalPngUrl(event->mimeData())) {
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
  const QMimeData* mime_data = event->mimeData();
  if (!hasLocalPngUrl(mime_data)) {
    event->ignore();
    return;
  }

  for (const QUrl& url : mime_data->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QFileInfo file_info(url.toLocalFile());
    if (!file_info.isFile() ||
        file_info.suffix().compare(QStringLiteral("png"),
                                   Qt::CaseInsensitive) != 0) {
      continue;
    }
    if (openFile(file_info.absoluteFilePath())) {
      event->acceptProposedAction();
      return;
    }
    QMessageBox::warning(this, QStringLiteral("PNG Analyzer"),
                         QStringLiteral("Could not open file:\n%1")
                             .arg(file_info.absoluteFilePath()));
    event->ignore();
    return;
  }
  event->ignore();
}

void MainWindow::onStageDone(std::uint64_t generation) {
  if (generation != session_->generation() || session_->stageSet() == nullptr) {
    return;  // stale stage analysis; never overwrite the current document
  }
  // The session opened its query coordinator before publishing the stages;
  // hand the non-null pointer to the selection controller first.
  selection_->setQueryCoordinator(session_->queryCoordinator());
  selection_->onStageSetPublished(session_->stageSet());
  trace_controller_->setQueryCoordinator(session_->queryCoordinator());
}

void MainWindow::onDecodeDone(std::uint64_t generation) {
  if (generation != session_->generation()) {
    return;  // stale decode; never overwrite the current document's image
  }
  const auto& result = session_->decodeResult();
  if (!result.success) {
    image_view_->setImage(QImage());
    selection_->setDefaultPixelStatus(QStringLiteral("decode failed: %1")
                                          .arg(QString::fromStdString(
                                              result.error)));
    return;
  }
  const auto& img = result.image;
  QImage qimage(static_cast<int>(img.width), static_cast<int>(img.height),
                QImage::Format_RGBA8888);
  std::memcpy(qimage.bits(), img.rgba.data(), img.rgba.size());
  image_view_->setImage(qimage);
  // Feed the delivered RGBA to the stage inspector's Delivered stage.
  inspector_->setDeliveredPixels(img.width, img.height, img.rgba);
  selection_->setDefaultPixelStatus(
      QStringLiteral("%1 x %2  (bit depth %3, color type %4)")
          .arg(img.width)
          .arg(img.height)
          .arg(img.source_bit_depth)
          .arg(img.source_color_type));
  // Establish a deterministic initial provenance target as soon as the
  // delivered image is available. The stage/query/trace workers may finish
  // in either order; the trace request is recorded while they are pending
  // and openTraceCoordinator() replays it once both indexes exist. This
  // keeps Compression populated immediately after opening a document
  // instead of requiring an incidental image click first.
  selection_->onPixelSelected(0, 0);
}

void MainWindow::onChunkDetailDone(std::uint64_t generation,
                                   std::uint64_t selection_serial) {
  if (generation != session_->generation() ||
      selection_serial != selection_->chunkSelectionSerial() ||
      chunk_detail_ == nullptr) {
    return;
  }
  chunk_detail_->setDetail(session_->chunkDetail());
}

void MainWindow::onValidationDone(std::uint64_t generation) {
  if (generation != session_->generation()) {
    return;
  }
  const pnga::analysis_engine::DocumentValidationReport& validation_report =
      session_->validationReport();
  if (validation_report.issues.empty()) {
    validation_label_->setText(QStringLiteral("Validation: OK"));
    validation_label_->setToolTip(QStringLiteral("No validation issues"));
  } else {
    const auto& first = validation_report.issues.front();
    validation_label_->setText(
        QStringLiteral("Validation: %1 issue(s), %2 @ %3")
            .arg(static_cast<qulonglong>(validation_report.issues.size()))
            .arg(QString::fromStdString(first.rule_id))
            .arg(static_cast<qulonglong>(first.offset)));
    QString tooltip;
    for (const auto& issue : validation_report.issues) {
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
}

void MainWindow::onRowQueryStatus(std::uint64_t row, int status) {
  // Worker-thread callback bridged to the GUI thread; show the latest status.
  inspector_->setRowQueryStatus(QLatin1String(
      pnga::analysis_engine::query_status_text(
          static_cast<pnga::analysis_engine::QueryStatus>(status))));
  (void)row;
}

bool MainWindow::openFile(const QString& path) {
  // Capture before replace(): the session adopts the new path on success and
  // the previous suffix must still be stripped from the title.
  const QString previous_path = session_->currentFilePath();
  if (!session_->replace(path)) {
    return false;
  }
  const std::uint64_t generation = session_->generation();
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  workspace_->rememberOpenedFile(path);
  if (!previous_path.isEmpty()) {
    const QString previous_suffix =
        QStringLiteral(" — %1").arg(previous_path);
    if (windowTitle().endsWith(previous_suffix)) {
      setWindowTitle(windowTitle().left(windowTitle().size() -
                                        previous_suffix.size()));
    }
  }
  setWindowTitle(QStringLiteral("%1 — %2").arg(windowTitle(), absolute_path));
  if (close_action_ != nullptr) {
    close_action_->setEnabled(true);
  }
  selection_->setDefaultPixelStatus(QStringLiteral("Loading image…"));
  bus_->setDocumentGeneration(generation);
  selection_->setDocument(generation, session_->source(), &session_->index(),
                          session_->queryCoordinator());
  trace_controller_->replaceDocument(generation, session_->source());
  // replace() cleared the session's query coordinator; drop the controller's
  // stale pointer immediately. onStageDone() supplies the fresh one.
  trace_controller_->setQueryCoordinator(session_->queryCoordinator());
  pixel_view_->clear();
  filtered_view_->clear();
  defiltered_view_->clear();
  image_view_->clearHoverPixel();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  selection_->replaceChunkModel(&session_->index());
  // A newly opened document always starts at the two primary views. This is
  // intentionally independent of the saved workspace tab from the previous
  // document, so Image and Reconstruction are visible immediately.
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  validation_label_->setText(QStringLiteral("Validation: checking…"));
  validation_label_->setToolTip(QString());
  // Start the primary workers only after the visual reset above.
  session_->startPrimaryWorkers();
  return true;
}

void MainWindow::onCloseTriggered() {
  if (!session_->hasDocument() && session_->currentFilePath().isEmpty()) {
    return;
  }
  // The session owns the path; capture it before close() clears the state so
  // the title suffix can be removed.
  const QString closing_path = session_->currentFilePath();

  session_->close();
  const std::uint64_t generation = session_->generation();
  bus_->setDocumentGeneration(generation);
  selection_->clearDocument(generation);
  trace_controller_->clearDocument(generation);

  inspector_->clear();
  pixel_view_->clear();
  filtered_view_->clear();
  defiltered_view_->clear();
  image_view_->setImage(QImage());
  image_view_->clearHoverPixel();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  selection_->replaceChunkModel(&session_->index());
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  selection_->setDefaultPixelStatus(QStringLiteral("No image"));
  validation_label_->setText(QStringLiteral("Validation: not loaded"));
  validation_label_->setToolTip(QString());

  const QString path_suffix = QStringLiteral(" — %1").arg(closing_path);
  if (!closing_path.isEmpty() && windowTitle().endsWith(path_suffix)) {
    setWindowTitle(windowTitle().left(windowTitle().size() - path_suffix.size()));
  }
  if (close_action_ != nullptr) {
    close_action_->setEnabled(false);
  }
}

void MainWindow::onOpenTriggered() {
  // Development builds may be launched directly from inside the .app bundle
  // and do not carry the production bundle metadata used by NSOpenPanel. Use
  // Qt's dialog in that case so File -> Open remains visible and modal on
  // macOS as well as on the other desktop platforms.
  raise();
  activateWindow();
  // Keep the picker constrained to PNG images. Including an "All files"
  // name-filter makes every file selectable, which defeats the Open PNG
  // workflow (especially with Qt's non-native dialog used by development
  // builds).
  // Set this option before any other dialog property. On macOS, setting it
  // after the constructor has already installed the name filter can leave the
  // native panel selected, where QFileDialog's name filter is ignored.
  QFileDialog dialog(this);
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
  dialog.setWindowTitle(QStringLiteral("Open PNG"));
  dialog.setDirectory(workspace_->lastOpenDirectory());
  dialog.setNameFilter(QStringLiteral("PNG files (*.png *.PNG)"));
  // QFileSystemModel disables non-matching files by default. That is the
  // behavior seen in the macOS picker; make the Qt-backed dialog hide them
  // instead while retaining directory entries for navigation.
  if (auto* file_system_model = dialog.findChild<QFileSystemModel*>();
      file_system_model != nullptr) {
    file_system_model->setNameFilterDisables(false);
  }
  dialog.setFileMode(QFileDialog::ExistingFile);
  const QString previous_file = workspace_->lastOpenFile();
  if (!previous_file.isEmpty()) {
    dialog.selectFile(previous_file);
  }
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected_files = dialog.selectedFiles();
  const QString path =
      selected_files.isEmpty() ? QString() : selected_files.front();
  if (path.isEmpty()) {
    return;
  }
  // Preserve the dialog location even when the selected file turns out to be
  // unreadable, so the next Open action still starts in the same directory.
  workspace_->rememberLastOpenDirectory(path);
  if (!openFile(path)) {
    QMessageBox::warning(this, QStringLiteral("PNG Analyzer"),
                         QStringLiteral("Could not open file:\n%1").arg(path));
  }
}
