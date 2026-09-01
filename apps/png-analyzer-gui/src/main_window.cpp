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

constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;

std::filesystem::path filesystemPath(const QString& path) {
#if defined(Q_OS_WIN)
  // std::filesystem::path's narrow constructor follows the active Windows
  // code page. Preserve Qt's UTF-16 path losslessly for Unicode filenames.
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

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

// Bounded Deep Trace budgets (WP-5U13). max_tokens is the primary row bound;
// the output budget caps how much of the deflate stream a replay decodes.
constexpr std::uint64_t kMaxTraceTokens = 4096;
// Keep enough bounded replay budget for wide RGB rows near the end of a
// moderately large image while retaining a hard upper bound.
constexpr std::uint64_t kTraceOutputBudgetBytes = 1ull << 23;   // 8 MiB
constexpr std::uint64_t kTraceIndexOutputBytes = 1ull << 26;    // 64 MiB

std::optional<std::uint64_t> filtered_output_offset_for_pixel(
    const pnga::analysis_engine::ScanlineAnchorIndexResult& anchors,
    const pnga::trace_model::ImageCoordinate& coordinate,
    std::uint64_t stream_row) {
  const auto channels = pnga::png_reconstruction::channels_for_color_type(
      anchors.header.color_type);
  if (channels == 0 || anchors.layout.pass_count == 0 ||
      stream_row >= anchors.scanlines.size()) {
    return std::nullopt;
  }

  std::uint64_t row_cursor = 0;
  for (std::size_t pass_index = 0;
       pass_index < anchors.layout.pass_count; ++pass_index) {
    const auto& pass = anchors.layout.passes[pass_index];
    if (pass.height == 0) {
      continue;
    }
    std::uint64_t pass_end = 0;
    if (pass.height > std::numeric_limits<std::uint64_t>::max() - row_cursor) {
      return std::nullopt;
    }
    pass_end = row_cursor + pass.height;
    if (stream_row < row_cursor || stream_row >= pass_end) {
      row_cursor = pass_end;
      continue;
    }
    if (coordinate.x < pass.x_start || coordinate.y < pass.y_start ||
        pass.x_step == 0 || pass.y_step == 0 ||
        (coordinate.x - pass.x_start) % pass.x_step != 0 ||
        (coordinate.y - pass.y_start) % pass.y_step != 0) {
      return std::nullopt;
    }
    const std::uint64_t local_x =
        (coordinate.x - pass.x_start) / pass.x_step;
    const std::uint64_t row_in_pass =
        (coordinate.y - pass.y_start) / pass.y_step;
    if (local_x >= pass.width || row_in_pass >= pass.height ||
        row_cursor + row_in_pass != stream_row) {
      return std::nullopt;
    }

    std::uint64_t sample_bits = 0;
    if (local_x != 0 &&
        static_cast<std::uint64_t>(channels) >
            std::numeric_limits<std::uint64_t>::max() / local_x) {
      return std::nullopt;
    }
    sample_bits = local_x * static_cast<std::uint64_t>(channels);
    if (anchors.header.bit_depth != 0 &&
        sample_bits > std::numeric_limits<std::uint64_t>::max() /
                          anchors.header.bit_depth) {
      return std::nullopt;
    }
    sample_bits *= anchors.header.bit_depth;
    const std::uint64_t sample_byte = sample_bits / 8;
    const auto& scanline = anchors.scanlines[stream_row];
    if (scanline.offset > std::numeric_limits<std::uint64_t>::max() - 1 ||
        scanline.offset + 1 >
            std::numeric_limits<std::uint64_t>::max() - sample_byte) {
      return std::nullopt;
    }
    return scanline.offset + 1 + sample_byte;
  }
  return std::nullopt;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> byte_range_for_bits(
    std::uint64_t bit_begin, std::uint64_t bit_end,
    std::uint64_t byte_origin = 0) noexcept {
  if (bit_end <= bit_begin ||
      bit_end > std::numeric_limits<std::uint64_t>::max() - 7) {
    return std::nullopt;
  }
  const std::uint64_t begin = bit_begin / 8;
  const std::uint64_t end = (bit_end + 7) / 8;
  if (byte_origin > std::numeric_limits<std::uint64_t>::max() - begin ||
      byte_origin > std::numeric_limits<std::uint64_t>::max() - end) {
    return std::nullopt;
  }
  const std::uint64_t start = byte_origin + begin;
  const std::uint64_t finish = byte_origin + end;
  if (finish <= start) {
    return std::nullopt;
  }
  return std::pair<std::uint64_t, std::uint64_t>{start, finish - start};
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

  trace_state_ =
      std::make_unique<pnga::analysis_engine::TraceInspectorStateMachine>();
  workspace_ = std::make_unique<WorkspaceController>(
      *this, widgets_,
      [this](const QString& path) { openRecentFile(path); });
  view_state_ = &workspace_->viewState();
  connect(block_inspector_,
          &pnga::ui::qt::BlockInspector::showInHexSpansRequested, this,
          [this](const QVector<QPair<quint64, quint64>>& ranges) {
            if (ranges.isEmpty()) {
              return;
            }
            setHexSource(pnga::ui::qt::HexSource::kFile);
            hex_->clearHighlight();
            std::vector<pnga::ui::qt::HexHighlightSpan> highlights;
            highlights.reserve(static_cast<std::size_t>(ranges.size()));
            for (const auto& range : ranges) {
              if (range.second != 0) {
                highlights.push_back({range.first, range.second,
                                      QColor(0x42, 0xA5, 0xF5)});
              }
            }
            hex_->navigateTo(ranges.front().first);
            hex_->setHighlight(std::move(highlights));
          });
  connect(block_inspector_,
          &pnga::ui::qt::BlockInspector::showInDeflateRequested, this,
          [this](quint64 bit_begin, quint64 bit_end) {
            // Block input bits are absolute logical (zlib/IDAT) stream bits,
            // including the zlib header.
            setHexSource(pnga::ui::qt::HexSource::kIdatStream);
            const auto range = byte_range_for_bits(bit_begin, bit_end);
            if (!range.has_value()) {
              return;
            }
            hex_->setHighlight({{range->first, range->second,
                                 QColor(0x42, 0xA5, 0xF5)}});
            hex_->navigateTo(range->first);
          });
  connect(decode_trace_inspector_,
          &pnga::ui::qt::DecodeTraceInspector::showInHexRequested, this,
          [this](quint64 output_begin, quint64 /*output_end*/) {
            setHexSource(pnga::ui::qt::HexSource::kInflated);
            hex_->navigateTo(output_begin);
          });
  connect(decode_trace_inspector_,
          &pnga::ui::qt::DecodeTraceInspector::showInDeflateRequested, this,
          [this](quint64 bit_begin, quint64 bit_end) {
            // Token input bits are relative to the start of the Deflate data
            // (after the zlib wrapper); add deflate_data_begin for the IDAT
            // byte offset.
            setHexSource(pnga::ui::qt::HexSource::kIdatStream);
            const auto range = byte_range_for_bits(
                bit_begin, bit_end, trace_deflate_data_begin_);
            if (!range.has_value()) {
              return;
            }
            hex_->setHighlight({{range->first, range->second,
                                 QColor(0x42, 0xA5, 0xF5)}});
            hex_->navigateTo(range->first);
          });

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
            view_state_->set_hover(coordinate);
            setPixelStatus(x, y);
          });
  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelHoverLeft,
          this, [this] {
            view_state_->clear_hover();
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
  preview_tabs_->installEventFilter(this);
  hex_source_tabs_->installEventFilter(this);
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
        view_state_->numeric_base != pnga::ui::qt::NumericBase::kHexadecimal;
    view_state_->numeric_base =
        hexadecimal ? pnga::ui::qt::NumericBase::kHexadecimal
                    : pnga::ui::qt::NumericBase::kDecimal;
    inspector_->setNumericBase(hexadecimal);
    pixel_view_->setNumericBase(hexadecimal);
    filtered_view_->setNumericBase(hexadecimal);
    defiltered_view_->setNumericBase(hexadecimal);
    updateNumericBaseButton();
  });
  connect(hex_source_tabs_, &pnga::ui::qt::HexSourceTabBar::sourceChanged,
          this, [this](pnga::ui::qt::HexSource source) {
            view_state_->hex_source = source;
            updateHexSource();
          });

  resize(1200, 760);
  workspace_->applyDefaults();
  workspace_->restore();
  updateHexSource();
  workspace_->configureDockInteraction();
}

MainWindow::~MainWindow() = default;


void MainWindow::updateHexSource() {
  if (hex_source_tabs_ != nullptr) {
    hex_source_tabs_->setSource(view_state_->hex_source);
  }
  if (source_ == nullptr) {
    hex_->setSource(nullptr);
    return;
  }
  const std::shared_ptr<const pnga::io::IByteSource> source = source_;
  if (view_state_->hex_source == pnga::ui::qt::HexSource::kIdatStream) {
    const pnga::png_format::VirtualIDATStream stream(index_);
    hex_->setSource(pnga::ui::qt::make_idat_hex_source(source, stream));
  } else if (view_state_->hex_source == pnga::ui::qt::HexSource::kInflated) {
    hex_->setSource(pnga::ui::qt::make_inflated_hex_source(stage_set_));
  } else if (view_state_->hex_source ==
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
  workspace_->applyDefaults();
  updateHexSource();
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

void MainWindow::updateNumericBaseButton() {
  if (base_button_ == nullptr) {
    return;
  }
  const bool hexadecimal =
      view_state_->numeric_base == pnga::ui::qt::NumericBase::kHexadecimal;
  const QString target = hexadecimal ? QStringLiteral("DEC")
                                     : QStringLiteral("HEX");
  base_button_->setText(target);
  base_button_->setToolTip(QStringLiteral("Switch to %1").arg(target));
}

void MainWindow::publishLockedCoordinate() {
  const pnga::trace_model::ImageCoordinate coordinate{
      0, 0, 0, static_cast<std::uint64_t>(x_spin_->value()),
      static_cast<std::uint64_t>(y_spin_->value())};
  if (!view_state_->set_locked(coordinate)) {
    return;
  }
  image_view_->setLockedPixel(
      QPoint(static_cast<int>(coordinate.x), static_cast<int>(coordinate.y)));
  pixel_view_->setCoordinate(coordinate.x, coordinate.y);
  filtered_view_->setCoordinate(coordinate.x, coordinate.y);
  defiltered_view_->setCoordinate(coordinate.x, coordinate.y);
  inspector_->onPixelSelected(coordinate.x, coordinate.y);
  pnga::trace_model::Selection update;
  update.image = coordinate;
  update.stage = pnga::trace_model::Stage::kDelivered;
  bus_->publishMerged(kImagePanelOrigin, generation_, update);
  requestTraceFor(coordinate);
}

void MainWindow::clearLockedCoordinate() {
  view_state_->clear_locked();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  if (view_state_->hover.has_value()) {
    setPixelStatus(static_cast<int>(view_state_->hover->x),
                   static_cast<int>(view_state_->hover->y));
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
  if (!view_state_->locked.has_value() || image_view_->image().isNull()) {
    return;
  }
  const QImage image = image_view_->image();
  std::uint64_t x = view_state_->locked->x;
  std::uint64_t y = view_state_->locked->y;
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
       watched == base_button_ || watched == preview_tabs_ ||
       watched == hex_source_tabs_ || watched == inspector_tabs_) &&
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
  workspace_->save();
  QMainWindow::closeEvent(event);
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
  filtered_view_->setStageSet(stage);
  defiltered_view_->setStageSet(stage);
  updateHexSource();
  // updateHexSource() clears the hex highlight; restore the selected chunk's
  // physical highlight (e.g. the default IHDR) so the hex view keeps showing
  // it after the stage worker refreshes the source.
  const QModelIndex current_chunk = tree_->selectionModel()->currentIndex();
  if (current_chunk.isValid() &&
      view_state_->hex_source == pnga::ui::qt::HexSource::kFile) {
    applyChunkHexHighlight(model_->chunkAt(current_chunk.row()));
  }
  stage_worker_ = nullptr;
  openQueryCoordinator(header);
  openTraceCoordinator();
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
  const std::error_code ec =
      pnga::io::open_mapped_file(filesystemPath(path), opened);
  if (ec) {
    return false;
  }
  // Shared ownership so an in-flight worker keeps its source alive even when a
  // newer file replaces source_ (virtual dtor makes this safe).
  auto source =
      std::shared_ptr<pnga::io::IByteSource>(opened.release());
  const QString absolute_path = QFileInfo(path).absoluteFilePath();
  workspace_->rememberOpenedFile(path);
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
  if (close_action_ != nullptr) {
    close_action_->setEnabled(true);
  }
  default_pixel_status_ = QStringLiteral("Loading image…");
  pixel_label_->setText(default_pixel_status_);
  source_ = source;
  index_ = pnga::png_format::index_chunks(*source_);
  ++generation_;
  bus_->setDocumentGeneration(generation_);
  view_state_->set_document_generation(generation_);
  trace_.reset();
  trace_result_.reset();
  if (trace_state_ != nullptr) {
    trace_state_->replaceDocument(generation_);
  }
  trace_handle_.reset();
  pending_trace_coordinate_.reset();
  trace_scanline_.reset();
  trace_selected_output_offset_.reset();
  trace_interval_.reset();
  trace_request_generation_ = 0;
  trace_deflate_data_begin_ = 0;
  if (trace_binding_ != nullptr) {
    trace_binding_->clear();
    trace_binding_->setHasDocument(true);
  }
  stage_set_.reset();
  pixel_view_->clear();
  filtered_view_->clear();
  defiltered_view_->clear();
  image_view_->clearHoverPixel();
  image_view_->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(lock_check_);
    lock_check_->setChecked(false);
  }
  resetDocument();
  // A newly opened document always starts at the two primary views. This is
  // intentionally independent of the saved workspace tab from the previous
  // document, so Image and Reconstruction are visible immediately.
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  validation_report_ = {};
  validation_label_->setText(QStringLiteral("Validation: checking…"));
  validation_label_->setToolTip(QString());
  startValidation();
  startDecode();
  startStageAnalysis();
  return true;
}

void MainWindow::onCloseTriggered() {
  if (source_ == nullptr && current_file_path_.isEmpty()) {
    return;
  }

  ++generation_;
  bus_->setDocumentGeneration(generation_);
  view_state_->set_document_generation(generation_);
  source_.reset();
  index_ = {};
  stage_set_.reset();
  query_.reset();
  trace_.reset();
  trace_handle_.reset();
  trace_result_.reset();
  pending_trace_coordinate_.reset();
  trace_scanline_.reset();
  trace_selected_output_offset_.reset();
  trace_interval_.reset();
  trace_request_generation_ = 0;
  trace_deflate_data_begin_ = 0;
  decode_worker_ = nullptr;
  stage_worker_ = nullptr;
  validation_worker_ = nullptr;
  chunk_detail_worker_ = nullptr;
  if (trace_state_ != nullptr) {
    trace_state_->replaceDocument(generation_);
  }
  if (trace_binding_ != nullptr) {
    trace_binding_->clear();
    trace_binding_->setHasDocument(false);
  }

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
  view_state_->clear_hover();
  view_state_->clear_locked();
  resetDocument();
  preview_tabs_->setCurrentIndex(0);
  inspector_tabs_->setCurrentIndex(0);
  default_pixel_status_ = QStringLiteral("No image");
  pixel_label_->setText(default_pixel_status_);
  validation_report_ = {};
  validation_label_->setText(QStringLiteral("Validation: not loaded"));
  validation_label_->setToolTip(QString());

  const QString path_suffix = QStringLiteral(" — %1").arg(current_file_path_);
  if (!current_file_path_.isEmpty() && windowTitle().endsWith(path_suffix)) {
    setWindowTitle(windowTitle().left(windowTitle().size() - path_suffix.size()));
  }
  current_file_path_.clear();
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
  // Establish a deterministic initial provenance target as soon as the
  // delivered image is available. The stage/query/trace workers may finish
  // in either order; onPixelSelected() records the request while they are
  // pending and openTraceCoordinator() replays it once both indexes exist.
  // This keeps Compression populated immediately after opening a document
  // instead of requiring an incidental image click first.
  onPixelSelected(0, 0);
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

  // Chunk offsets are physical file offsets; never apply them to a virtual
  // IDAT or reconstructed stage address space.
  view_state_->hex_source = pnga::ui::qt::HexSource::kFile;
  updateHexSource();

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

  applyChunkHexHighlight(node);

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

void MainWindow::applyChunkHexHighlight(
    const pnga::png_format::ChunkNode& node) {
  if (hex_ == nullptr) {
    return;
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
  if (view_state_->locked.has_value()) {
    setPixelStatus(static_cast<int>(view_state_->locked->x),
                   static_cast<int>(view_state_->locked->y));
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
  view_state_->set_locked(*sel.image);
  image_view_->setLockedPixel(QPoint(x, y));
  pixel_view_->setCoordinate(static_cast<std::uint64_t>(x),
                             static_cast<std::uint64_t>(y));
  filtered_view_->setCoordinate(static_cast<std::uint64_t>(x),
                                static_cast<std::uint64_t>(y));
  defiltered_view_->setCoordinate(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
  bus_->publishMerged(kImagePanelOrigin, generation_, sel);
  requestTraceFor(
      pnga::trace_model::ImageCoordinate{0, 0, 0, static_cast<std::uint64_t>(x),
                                         static_cast<std::uint64_t>(y)});
  setPixelStatus(x, y);
}

// ---------------------------------------------------------------------------
// WP-5U13: bounded trace pipeline wiring
// ---------------------------------------------------------------------------

void MainWindow::openTraceCoordinator() {
  trace_.reset();
  trace_handle_.reset();
  if (source_ == nullptr) {
    return;
  }
  const std::shared_ptr<const pnga::io::IByteSource> shared =
      std::shared_ptr<const pnga::io::IByteSource>(source_);
  auto trace = std::make_unique<pnga::analysis_engine::TraceOrchestrator>(
      /*worker_count=*/1,
      /*max_reserved_bytes=*/kTraceOutputBudgetBytes * 2);
  if (!trace->open(shared, kTraceIndexOutputBytes)) {
    return;
  }
  trace->setDocumentGeneration(generation_);
  if (trace_binding_ != nullptr) {
    trace_binding_->publishFastIndex(trace->fast_index());
  }
  // Bridge the worker-thread result callback onto the GUI thread. The queued
  // invoke is dropped automatically if this window is destroyed.
  trace->setResultCallback(
      [this](const pnga::analysis_engine::TraceQueryResult& result) {
        QMetaObject::invokeMethod(
            this, [this, result] { onTraceResult(result); },
            Qt::QueuedConnection);
      });
  trace_ = std::move(trace);
  if (pending_trace_coordinate_.has_value()) {
    const pnga::trace_model::ImageCoordinate pending =
        *pending_trace_coordinate_;
    pending_trace_coordinate_.reset();
    requestTraceFor(pending);
  }
}

void MainWindow::onTraceResult(
    const pnga::analysis_engine::TraceQueryResult& result) {
  if (result.generation != generation_ || trace_binding_ == nullptr ||
      trace_state_ == nullptr) {
    return;  // stale result; never publish for an older document
  }
  trace_deflate_data_begin_ = result.deflate_data_begin;
  trace_result_ = std::make_shared<const pnga::analysis_engine::TraceQueryResult>(
      result);
  const std::uint64_t selected_output_offset =
      trace_selected_output_offset_.value_or(result.inflated_begin);
  const bool accepted = trace_state_->publish(
      result, std::nullopt, selected_output_offset, trace_scanline_);
  if (accepted) {
    trace_binding_->publishState(trace_state_->state());
  }
  trace_handle_.reset();
}

void MainWindow::requestTraceFor(
    const pnga::trace_model::ImageCoordinate& coordinate) {
  if (trace_binding_ == nullptr || trace_state_ == nullptr) {
    return;
  }
  if (trace_ == nullptr || !trace_->has_index() || query_ == nullptr ||
      !query_->has_index()) {
    // The trace pipeline is not ready yet; remember the committed coordinate
    // and publish a not-indexed state instead of guessing an interval.
    pending_trace_coordinate_ = coordinate;
    trace_binding_->setNotIndexed(true);
    return;
  }
  trace_binding_->setNotIndexed(false);
  const auto row = pnga::analysis_engine::stream_row_for_pixel(
      query_->anchors().layout, coordinate.x, coordinate.y);
  if (!row.has_value()) {
    return;
  }
  const auto& scanlines = query_->anchors().scanlines;
  if (*row >= scanlines.size()) {
    return;
  }
  const std::uint64_t begin = scanlines[*row].offset;
  if (scanlines[*row].length >
      std::numeric_limits<std::uint64_t>::max() - begin) {
    trace_selected_output_offset_.reset();
    return;
  }
  const std::uint64_t end = begin + scanlines[*row].length;
  if (end <= begin) {
    trace_selected_output_offset_.reset();
    return;
  }
  trace_selected_output_offset_ = filtered_output_offset_for_pixel(
      query_->anchors(), coordinate, *row);
  if (trace_interval_.has_value() && trace_request_generation_ == generation_ &&
      trace_interval_->first == begin && trace_interval_->second == end) {
    if (trace_result_ != nullptr) {
      const bool accepted = trace_state_->publish(
          *trace_result_, std::nullopt,
          trace_selected_output_offset_.value_or(trace_result_->inflated_begin),
          trace_scanline_);
      if (accepted) {
        trace_binding_->publishState(trace_state_->state());
      }
    }
    return;  // identical committed interval already requested (dedup)
  }
  pending_trace_coordinate_.reset();
  trace_interval_ = std::make_pair(begin, end);
  trace_scanline_ = *row;
  trace_request_generation_ = generation_;
  if (trace_handle_ != nullptr && trace_handle_->accepted()) {
    trace_->cancel(*trace_handle_);
    trace_handle_.reset();
  }
  pnga::analysis_engine::TraceOrchestrationRequest request;
  request.generation = generation_;
  pnga::trace_model::Selection selection;
  selection.image = coordinate;
  selection.stage = pnga::trace_model::Stage::kDelivered;
  request.selection = selection;
  request.inflated_begin = begin;
  request.inflated_end = end;
  request.max_tokens = kMaxTraceTokens;
  request.trace_output_budget_bytes = kTraceOutputBudgetBytes;
  request.priority = pnga::analysis_engine::JobPriority::kSelection;
  trace_state_->markReplaying(generation_);
  trace_binding_->publishState(trace_state_->state());
  const auto handle = trace_->submit(request);
  trace_handle_ = handle.accepted()
                      ? std::make_unique<pnga::analysis_engine::TraceTaskHandle>(
                            handle)
                      : nullptr;
}

void MainWindow::setHexSource(pnga::ui::qt::HexSource source) {
  view_state_->hex_source = source;
  if (hex_source_tabs_ != nullptr) {
    hex_source_tabs_->setSource(source);
  }
  updateHexSource();
}
