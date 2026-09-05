// WP-104/204 facade, decomposed by WP-5U15: a thin QMainWindow composition
// root wiring MainWindowWidgets to the workspace, session, selection and
// trace controllers. Decoding stays off the UI thread; stale results are
// generation-gated.

#include "main_window.h"

#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/chunk_detail_panel.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>

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

MainWindow::MainWindow(QWidget* parent,
                       pnga::ui::qt::ApplicationTheme* theme)
    : QMainWindow(parent), compression_store_(this) {
  widgets_ = buildMainWindowUi(*this, theme);
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
            trace_->requestFor(coordinate);
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
            widgets_.inspector->setRowQueryStatus(QLatin1String(
                pnga::analysis_engine::query_status_text(result.status)));
          },
          [this](const pnga::png_format::ChunkNode& node,
                 std::uint64_t selection_serial) {
            session_->requestChunkDetail(node, selection_serial);
          }},
      this, &workspace_->viewState(), &compression_store_);
  trace_ = std::make_unique<TraceController>(widgets_, this);
  connect(trace_.get(), &TraceController::hexSourceRequested,
          selection_.get(), &SelectionNavigationController::setHexSource);
  connect(widgets_.open_action, &QAction::triggered, this,
          &MainWindow::onOpenTriggered);
  connect(widgets_.close_action, &QAction::triggered, this,
          &MainWindow::onCloseTriggered);
  connect(widgets_.recent_files_menu, &QMenu::aboutToShow, this,
          [this] { workspace_->refreshRecentFilesMenu(); });
  workspace_->refreshRecentFilesMenu();
  connect(widgets_.exit_action, &QAction::triggered, this,
          [this] { close(); });

  connect(widgets_.reset_layout_action, &QAction::triggered, this,
          &MainWindow::resetLayout);
  connect(widgets_.show_hex_view_action, &QAction::toggled, this,
          [this](bool visible) { widgets_.hex_panel->setVisible(visible); });
  if (theme != nullptr) {
    const auto connectThemeAction = [this, theme](QAction* action) {
      const auto mode = static_cast<pnga::ui::qt::ApplicationTheme::ThemeMode>(
          action->data().toInt());
      connect(action, &QAction::triggered, this, [this, theme, mode] {
        theme->setMode(mode);
      });
    };
    connectThemeAction(widgets_.theme_system_action);
    connectThemeAction(widgets_.theme_light_action);
    connectThemeAction(widgets_.theme_dark_action);
    const auto updateThemeActions = [this, theme] {
      widgets_.theme_system_action->setChecked(
          theme->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
      widgets_.theme_light_action->setChecked(
          theme->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
      widgets_.theme_dark_action->setChecked(
          theme->requestedMode() ==
          pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
    };
    connect(theme, &pnga::ui::qt::ApplicationTheme::themeChanged, this,
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

  connect(widgets_.image_view, &pnga::ui::qt::DeliveredImageView::pixelSelected,
          selection_.get(), &SelectionNavigationController::onPixelSelected);
  connect(widgets_.image_view, &pnga::ui::qt::DeliveredImageView::pixelHovered,
          selection_.get(), &SelectionNavigationController::onPixelHovered);
  connect(widgets_.image_view, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
          selection_.get(), &SelectionNavigationController::onPixelHoverLeft);
  connect(widgets_.image_view,
          &pnga::ui::qt::DeliveredImageView::pixelNudgeRequested,
          selection_.get(), &SelectionNavigationController::nudgeLockedCoordinate);
  connect(widgets_.image_view,
          &pnga::ui::qt::DeliveredImageView::selectionCancelled,
          selection_.get(),
          &SelectionNavigationController::clearLockedCoordinate);
  widgets_.x_spin->installEventFilter(this);
  widgets_.y_spin->installEventFilter(this);
  widgets_.lock_check->installEventFilter(this);
  widgets_.base_button->installEventFilter(this);
  widgets_.preview_tabs->installEventFilter(this);
  widgets_.hex_source_tabs->installEventFilter(this);
  widgets_.inspector_tabs->installEventFilter(this);
  widgets_.chunks_dock->installEventFilter(this);
  widgets_.inspector_dock->installEventFilter(this);

  connect(widgets_.x_spin, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (widgets_.lock_check->isChecked()) {
              selection_->publishLockedCoordinate();
            }
          });
  connect(widgets_.y_spin, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) {
            if (widgets_.lock_check->isChecked()) {
              selection_->publishLockedCoordinate();
            }
          });
  connect(widgets_.lock_check, &QCheckBox::toggled, this, [this](bool locked) {
    if (locked) {
      selection_->publishLockedCoordinate();
    } else {
      selection_->clearLockedCoordinate();
    }
  });
  connect(widgets_.base_button, &QPushButton::clicked, selection_.get(),
          &SelectionNavigationController::toggleNumericBase);
  connect(widgets_.hex_source_tabs, &pnga::ui::qt::HexSourceTabBar::sourceChanged,
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

  drawDots(widgets_.chunks_dock);
  drawDots(widgets_.inspector_dock);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // A floating dock keeps the main window layout frozen until something
  // clears QMainWindowLayout::savedState; Qt's title-bar double-click would
  // then plug the dock in place at its dragged position. Re-dock through the
  // same path Reset Layout uses: plug the dock, pin it to the area its
  // placeholder still bookkeeps (the previous dock position) and normalize
  // the frozen drag state so the layout geometry is applied. Consuming the
  // event keeps Qt's default double-click handler from plugging again.
  // Both the client-area and the native non-client-area event are handled:
  // macOS generates the latter for the floating title bar.
  if ((watched == widgets_.chunks_dock ||
       watched == widgets_.inspector_dock) &&
      (event->type() == QEvent::MouseButtonDblClick ||
       event->type() == QEvent::NonClientAreaMouseButtonDblClick)) {
    auto* dock = qobject_cast<QDockWidget*>(watched);
    if (dock != nullptr && dock->isFloating()) {
      const Qt::DockWidgetArea previous_area = dockWidgetArea(dock);
      dock->setFloating(false);
      if (previous_area != Qt::NoDockWidgetArea) {
        addDockWidget(previous_area, dock);
      }
      workspace_->normalize_frozen_dock_state();
      event->accept();
      return true;
    }
  }
  if ((watched == widgets_.x_spin || watched == widgets_.y_spin || watched == widgets_.lock_check ||
       watched == widgets_.base_button || watched == widgets_.preview_tabs ||
       watched == widgets_.hex_source_tabs || watched == widgets_.inspector_tabs) &&
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
  trace_->setQueryCoordinator(session_->queryCoordinator());
}

void MainWindow::onDecodeDone(std::uint64_t generation) {
  if (generation != session_->generation()) {
    return;  // stale decode; never overwrite the current document's image
  }
  const auto& result = session_->decodeResult();
  if (!result.success) {
    widgets_.image_view->setImage(QImage());
    selection_->setDefaultPixelStatus(QStringLiteral("decode failed: %1")
                                          .arg(QString::fromStdString(
                                              result.error)));
    return;
  }
  const auto& img = result.image;
  QImage qimage(static_cast<int>(img.width), static_cast<int>(img.height),
                QImage::Format_RGBA8888);
  std::memcpy(qimage.bits(), img.rgba.data(), img.rgba.size());
  widgets_.image_view->setImage(qimage);
  // Feed the delivered RGBA to the stage inspector's Delivered stage.
  widgets_.inspector->setDeliveredPixels(img.width, img.height, img.rgba);
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
      widgets_.chunk_detail == nullptr) {
    return;
  }
  widgets_.chunk_detail->setDetail(session_->chunkDetail());
}

void MainWindow::onValidationDone(std::uint64_t generation) {
  if (generation != session_->generation()) {
    return;
  }
  const pnga::analysis_engine::DocumentValidationReport& validation_report =
      session_->validationReport();
  if (validation_report.issues.empty()) {
    widgets_.validation_label->setText(QStringLiteral("Validation: OK"));
    widgets_.validation_label->setToolTip(QStringLiteral("No validation issues"));
  } else {
    const auto& first = validation_report.issues.front();
    widgets_.validation_label->setText(
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
    widgets_.validation_label->setToolTip(tooltip);
  }
}

void MainWindow::onRowQueryStatus(std::uint64_t row, int status) {
  // Worker-thread callback bridged to the GUI thread; show the latest status.
  widgets_.inspector->setRowQueryStatus(QLatin1String(
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
  if (widgets_.close_action != nullptr) {
    widgets_.close_action->setEnabled(true);
  }
  selection_->setDefaultPixelStatus(QStringLiteral("Loading image…"));
  widgets_.bus->setDocumentGeneration(generation);
  selection_->setDocument(generation, session_->source(), &session_->index(),
                          session_->queryCoordinator());
  trace_->replaceDocument(generation, session_->source());
  // replace() cleared the session's query coordinator; drop the controller's
  // stale pointer immediately. onStageDone() supplies the fresh one.
  trace_->setQueryCoordinator(session_->queryCoordinator());
  widgets_.pixel_view->clear();
  widgets_.filtered_view->clear();
  widgets_.defiltered_view->clear();
  widgets_.image_view->clearHoverPixel();
  widgets_.image_view->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(widgets_.lock_check);
    widgets_.lock_check->setChecked(false);
  }
  selection_->replaceChunkModel(&session_->index());
  // A newly opened document always starts at the two primary views. This is
  // intentionally independent of the saved workspace tab from the previous
  // document, so Image and Reconstruction are visible immediately.
  widgets_.preview_tabs->setCurrentIndex(0);
  widgets_.inspector_tabs->setCurrentIndex(0);
  widgets_.validation_label->setText(QStringLiteral("Validation: checking…"));
  widgets_.validation_label->setToolTip(QString());
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
  widgets_.bus->setDocumentGeneration(generation);
  selection_->clearDocument(generation);
  trace_->clearDocument(generation);

  widgets_.inspector->clear();
  widgets_.pixel_view->clear();
  widgets_.filtered_view->clear();
  widgets_.defiltered_view->clear();
  widgets_.image_view->setImage(QImage());
  widgets_.image_view->clearHoverPixel();
  widgets_.image_view->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(widgets_.lock_check);
    widgets_.lock_check->setChecked(false);
  }
  selection_->replaceChunkModel(&session_->index());
  widgets_.preview_tabs->setCurrentIndex(0);
  widgets_.inspector_tabs->setCurrentIndex(0);
  selection_->setDefaultPixelStatus(QStringLiteral("No image"));
  widgets_.validation_label->setText(QStringLiteral("Validation: not loaded"));
  widgets_.validation_label->setToolTip(QString());

  const QString path_suffix = QStringLiteral(" — %1").arg(closing_path);
  if (!closing_path.isEmpty() && windowTitle().endsWith(path_suffix)) {
    setWindowTitle(windowTitle().left(windowTitle().size() - path_suffix.size()));
  }
  if (widgets_.close_action != nullptr) {
    widgets_.close_action->setEnabled(false);
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
