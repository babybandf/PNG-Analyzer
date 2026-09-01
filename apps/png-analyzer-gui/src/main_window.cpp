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
constexpr int kMaxRecentFiles = 10;
constexpr auto kRecentFilesSettingsKey = "file/recentFiles";
constexpr auto kLastOpenDirectorySettingsKey = "file/lastOpenDirectory";
constexpr auto kLastOpenFileSettingsKey = "file/lastOpenFile";

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

QString previewPageId(int index) {
  switch (index) {
    case 0:
      return QStringLiteral("image");
    case 1:
      return QStringLiteral("pixels");
    case 2:
      return QStringLiteral("filtered");
    case 3:
      return QStringLiteral("defiltered");
    default:
      return QString();
  }
}

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

int previewIndexForId(const QString& id) {
  if (id == QStringLiteral("image")) {
    return 0;
  }
  if (id == QStringLiteral("pixels")) {
    return 1;
  }
  if (id == QStringLiteral("filtered")) {
    return 2;
  }
  if (id == QStringLiteral("defiltered")) {
    return 3;
  }
  return 0;
}

QString inspectorPageId(int index) {
  switch (index) {
    case 0:
      return QStringLiteral("reconstruction");
    case 1:
      return QStringLiteral("compression");
    default:
      return QString();
  }
}

int inspectorIndexForId(const QString& id) {
  return id == QStringLiteral("compression") ? 1 : 0;
}

int migratePreviewIndexV1(int index) {
  switch (index) {
    case 1:
      return 1;  // Pixels
    case 3:
      return 2;  // Filtered
    case 4:
      return 3;  // Defiltered
    case 0:
    case 2:
    default:
      return 0;  // Image and the retired Filter Map
  }
}

int migrateInspectorIndexV1(int index) {
  return index == 2 ? 1 : 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent,
                       pnga::ui::qt::ApplicationTheme* theme)
    : QMainWindow(parent), theme_(theme) {
  setWindowTitle(QStringLiteral("PNG Analyzer"));
  setAcceptDrops(true);
  // Standalone layout tests construct MainWindow without the application
  // controller. Keep their separator contract while the product build uses
  // the centralized stylesheet installed by ApplicationTheme.
  if (qApp == nullptr || qApp->styleSheet().isEmpty()) {
    setStyleSheet(QStringLiteral(
        "QMainWindow::separator { width: 8px; height: 8px; "
        "background: transparent; }"
        "QMainWindow::separator:hover, QMainWindow::separator:pressed { "
        "background: palette(highlight); }"));
  }
  // QMainWindow creates its dock separators lazily when the window is laid
  // out.  Keep the hit target wide enough for a mouse even when a native
  // style would otherwise expose only a one-pixel separator.
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
  pixel_view_ = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kNative, preview_tabs_);
  preview_tabs_->addTab(pixel_view_, QStringLiteral("Pixels"));
  filtered_view_ = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kFiltered, preview_tabs_);
  preview_tabs_->addTab(filtered_view_, QStringLiteral("Filtered"));
  defiltered_view_ = new pnga::ui::qt::StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage::kDefiltered, preview_tabs_);
  preview_tabs_->addTab(defiltered_view_, QStringLiteral("Unfiltered"));

  hex_panel_ = new QWidget(center_splitter_);
  hex_panel_->setObjectName(QStringLiteral("hexPanel"));
  hex_panel_->setAccessibleName(QStringLiteral("Hex panel"));
  auto* hex_layout = new QHBoxLayout(hex_panel_);
  hex_layout->setContentsMargins(0, 0, 0, 0);
  hex_source_tabs_ = new pnga::ui::qt::HexSourceTabBar(hex_panel_);
  hex_layout->addWidget(hex_source_tabs_);
  hex_ = new pnga::ui::qt::HexView(hex_panel_);
  hex_->setObjectName(QStringLiteral("hexView"));
  hex_->setAccessibleName(QStringLiteral("Hex view"));
  hex_layout->addWidget(hex_, 1);
  center_splitter_->addWidget(preview_tabs_);
  center_splitter_->addWidget(hex_panel_);
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
  base_button_->setFixedWidth(base_button_->sizeHint().width());
  coordinate_layout->addWidget(base_button_);
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

  compression_inspector_tabs_ = new QTabWidget(inspector_container);
  compression_inspector_tabs_->setObjectName(
      QStringLiteral("compressionInspectorPages"));
  compression_inspector_tabs_->setAccessibleName(
      QStringLiteral("Compression inspector pages"));
  compression_inspector_tabs_->setUsesScrollButtons(true);

  inspector_ = new pnga::ui::qt::StageInspector(inspector_tabs_);
  inspector_->setObjectName(QStringLiteral("reconstructInspector"));
  inspector_->setAccessibleName(QStringLiteral("Reconstruct inspector"));
  inspector_tabs_->addTab(inspector_, QStringLiteral("Reconstruction"));
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
  // WP-5U12: the shared Compression context sits above the page stack so the
  // trace state is stated once, not repeated on every page.
  auto* compression_container = new QWidget(inspector_container);
  compression_container->setObjectName(QStringLiteral("compressionContainer"));
  compression_container->setAccessibleName(
      QStringLiteral("Compression inspector"));
  auto* compression_layout = new QVBoxLayout(compression_container);
  compression_layout->setContentsMargins(4, 2, 4, 2);
  compression_layout->setSpacing(2);
  compression_context_ = new pnga::ui::qt::CompressionContext(compression_container);
  compression_layout->addWidget(compression_context_);
  compression_layout->addWidget(compression_inspector_tabs_, 1);
  inspector_tabs_->addTab(compression_container, QStringLiteral("Compression"));
  // WP-5U13: bind the bounded trace pipeline to the three Compression pages.
  // The binding publishes one generation-coherent bundle; navigation keeps the
  // WP-5U11 source semantics (physical File for block spans, Inflated for
  // output bytes, IDAT for logical Deflate bits).
  trace_binding_ = new pnga::ui::qt::TraceInspectorBinding(
      block_inspector_, huffman_inspector_, decode_trace_inspector_, this);
  trace_binding_->setContext(compression_context_);
  trace_binding_->setHasDocument(false);
  trace_state_ =
      std::make_unique<pnga::analysis_engine::TraceInspectorStateMachine>();
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
  inspector_layout->addWidget(inspector_tabs_, 1);
  QWidget::setTabOrder(x_spin_, y_spin_);
  QWidget::setTabOrder(y_spin_, lock_check_);
  QWidget::setTabOrder(lock_check_, base_button_);
  QWidget::setTabOrder(base_button_, preview_tabs_);
  QWidget::setTabOrder(preview_tabs_, hex_source_tabs_);
  QWidget::setTabOrder(hex_source_tabs_, hex_);
  QWidget::setTabOrder(hex_, inspector_tabs_);
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
  close_action_ = fileMenu->addAction(QStringLiteral("&Close Image"));
  close_action_->setObjectName(QStringLiteral("closeImageAction"));
  close_action_->setShortcut(QKeySequence::Close);
  close_action_->setEnabled(false);
  connect(close_action_, &QAction::triggered, this,
          &MainWindow::onCloseTriggered);
  fileMenu->addSeparator();
  recent_files_menu_ = fileMenu->addMenu(QStringLiteral("Open Recent"));
  recent_files_menu_->setObjectName(QStringLiteral("recentFilesMenu"));
  recent_files_menu_->setToolTipsVisible(true);
  connect(recent_files_menu_, &QMenu::aboutToShow, this,
          &MainWindow::refreshRecentFilesMenu);
  refreshRecentFilesMenu();
  fileMenu->addSeparator();
  QAction* exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
  exitAction->setObjectName(QStringLiteral("exitAction"));
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, [this] { close(); });

  QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
  QAction* resetAction =
      viewMenu->addAction(QStringLiteral("&Reset Layout"));
  connect(resetAction, &QAction::triggered, this, &MainWindow::resetLayout);
  viewMenu->addSeparator();
  QAction* chunkListAction = chunks_dock_->toggleViewAction();
  viewMenu->addAction(chunkListAction);
  chunkListAction->setText(QStringLiteral("Chunk List"));
  chunkListAction->setObjectName(QStringLiteral("showChunkList"));
  QAction* hexViewAction = viewMenu->addAction(QStringLiteral("Hex View"));
  hexViewAction->setObjectName(QStringLiteral("showHexView"));
  hexViewAction->setCheckable(true);
  hexViewAction->setChecked(true);
  connect(hexViewAction, &QAction::toggled, this,
          [this](bool visible) { hex_panel_->setVisible(visible); });
  QAction* inspectorAction = inspector_dock_->toggleViewAction();
  viewMenu->addAction(inspectorAction);
  inspectorAction->setText(QStringLiteral("Inspector"));
  inspectorAction->setObjectName(QStringLiteral("showInspector"));
  viewMenu->addSeparator();
  if (theme_ != nullptr) {
    QMenu* themeMenu = viewMenu->addMenu(QStringLiteral("Theme"));
    themeMenu->setObjectName(QStringLiteral("themeMenu"));
    auto* group = new QActionGroup(themeMenu);
    group->setExclusive(true);
    const auto addThemeAction = [this, themeMenu, group](
                                    const QString& text,
                                    const QString& object_name,
                                    pnga::ui::qt::ApplicationTheme::ThemeMode mode) {
      QAction* action = themeMenu->addAction(text);
      action->setObjectName(object_name);
      action->setCheckable(true);
      action->setData(static_cast<int>(mode));
      group->addAction(action);
      connect(action, &QAction::triggered, this, [this, mode] {
        theme_->setMode(mode);
      });
      return action;
    };
    QAction* systemTheme = addThemeAction(
        QStringLiteral("Follow System"), QStringLiteral("themeSystem"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
    QAction* lightTheme = addThemeAction(
        QStringLiteral("Light"), QStringLiteral("themeLight"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
    QAction* darkTheme = addThemeAction(
        QStringLiteral("Dark"), QStringLiteral("themeDark"),
        pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
    const auto updateThemeActions = [this, systemTheme, lightTheme, darkTheme] {
      systemTheme->setChecked(theme_->requestedMode() ==
                              pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
      lightTheme->setChecked(theme_->requestedMode() ==
                             pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
      darkTheme->setChecked(theme_->requestedMode() ==
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
        view_state_.numeric_base != pnga::ui::qt::NumericBase::kHexadecimal;
    view_state_.numeric_base =
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
            view_state_.hex_source = source;
            updateHexSource();
          });

  resize(1200, 760);
  applyDefaultWorkspace();
  restoreWorkspace();
  configureDockInteraction();
}

MainWindow::~MainWindow() = default;

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
  view_state_.locked = preserved_locked;
  view_state_.hover = preserved_hover;
  {
    const QSignalBlocker base_blocker(base_button_);
    const QSignalBlocker lock_blocker(lock_check_);
    updateNumericBaseButton();
    lock_check_->setChecked(preserved_locked.has_value());
    if (preserved_locked.has_value()) {
      x_spin_->setValue(static_cast<int>(preserved_locked->x));
      y_spin_->setValue(static_cast<int>(preserved_locked->y));
    }
  }
  hex_source_tabs_->setSource(pnga::ui::qt::HexSource::kFile);
  inspector_->setNumericBase(false);
  pixel_view_->setNumericBase(false);
  filtered_view_->setNumericBase(false);
  defiltered_view_->setNumericBase(false);
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
  const int workspace_version =
      settings.value(QStringLiteral("workspace/version")).toInt();
  const bool has_saved =
      (workspace_version == 1 || workspace_version == 2) &&
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

  int preview_index = 0;
  int inspector_index = 0;
  if (workspace_version == 2) {
    preview_index = previewIndexForId(
        settings.value(QStringLiteral("workspace/previewTabId"),
                       QStringLiteral("image"))
            .toString());
    inspector_index = inspectorIndexForId(
        settings.value(QStringLiteral("workspace/inspectorPageId"),
                       QStringLiteral("reconstruction"))
            .toString());
  } else {
    preview_index = migratePreviewIndexV1(
        settings.value(QStringLiteral("workspace/previewTab"), 0).toInt());
    inspector_index = migrateInspectorIndexV1(
        settings.value(QStringLiteral("workspace/inspectorTab"), 0).toInt());
  }
  preview_tabs_->setCurrentIndex(preview_index);
  inspector_tabs_->setCurrentIndex(inspector_index);
  const int compression_page = settings.value(QStringLiteral("workspace/compressionPage"), 0).toInt();
  if (compression_page >= 0 &&
      compression_page < compression_inspector_tabs_->count()) {
    compression_inspector_tabs_->setCurrentIndex(compression_page);
  }

  const int base = settings.value(QStringLiteral("view/numericBase"), 0).toInt();
  const int source = settings.value(QStringLiteral("view/hexSource"), 0).toInt();
  view_state_.numeric_base = base == 1
                                 ? pnga::ui::qt::NumericBase::kHexadecimal
                                 : pnga::ui::qt::NumericBase::kDecimal;
  view_state_.hex_source = source >= 0 && source <= 3
                                ? static_cast<pnga::ui::qt::HexSource>(source)
                                : pnga::ui::qt::HexSource::kFile;
  {
    const QSignalBlocker base_blocker(base_button_);
    updateNumericBaseButton();
  }
  hex_source_tabs_->setSource(view_state_.hex_source);
  inspector_->setNumericBase(base == 1);
  pixel_view_->setNumericBase(base == 1);
  filtered_view_->setNumericBase(base == 1);
  defiltered_view_->setNumericBase(base == 1);
  updateHexSource();
}

void MainWindow::saveWorkspace() const {
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 2);
  settings.setValue(QStringLiteral("workspace/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("workspace/mainState"), saveState());
  settings.setValue(QStringLiteral("workspace/splitterState"),
                    center_splitter_->saveState());
  settings.setValue(QStringLiteral("workspace/chunkDetailSplitterState"),
                    chunks_splitter_->saveState());
  settings.setValue(QStringLiteral("workspace/previewTabId"),
                    previewPageId(preview_tabs_->currentIndex()));
  settings.setValue(QStringLiteral("workspace/inspectorPageId"),
                    inspectorPageId(inspector_tabs_->currentIndex()));
  settings.setValue(QStringLiteral("workspace/compressionPage"),
                    compression_inspector_tabs_->currentIndex());
  settings.setValue(QStringLiteral("view/numericBase"),
                    static_cast<int>(view_state_.numeric_base));
  settings.setValue(QStringLiteral("view/hexSource"),
                    static_cast<int>(view_state_.hex_source));
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
    if (info.isFile()) {
      settings.setValue(QLatin1String(kLastOpenFileSettingsKey),
                        info.absoluteFilePath());
    }
    settings.sync();
  }
}

QString MainWindow::lastOpenDirectory() const {
  QSettings settings;
  const QString directory =
      settings.value(QLatin1String(kLastOpenDirectorySettingsKey)).toString();
  return !directory.isEmpty() && QDir(directory).exists() ? directory
                                                           : QString();
}

QString MainWindow::lastOpenFile() const {
  QSettings settings;
  const QString path =
      settings.value(QLatin1String(kLastOpenFileSettingsKey)).toString();
  const QFileInfo info(path);
  return !path.isEmpty() && info.exists() && info.isFile()
             ? info.absoluteFilePath()
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
  settings.sync();
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
  if (hex_source_tabs_ != nullptr) {
    hex_source_tabs_->setSource(view_state_.hex_source);
  }
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
  saveWorkspace();
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
      view_state_.hex_source == pnga::ui::qt::HexSource::kFile) {
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
  if (close_action_ != nullptr) {
    close_action_->setEnabled(true);
  }
  default_pixel_status_ = QStringLiteral("Loading image…");
  pixel_label_->setText(default_pixel_status_);
  source_ = source;
  index_ = pnga::png_format::index_chunks(*source_);
  ++generation_;
  bus_->setDocumentGeneration(generation_);
  view_state_.set_document_generation(generation_);
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
  view_state_.set_document_generation(generation_);
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
  view_state_.clear_hover();
  view_state_.clear_locked();
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
  dialog.setDirectory(lastOpenDirectory());
  dialog.setNameFilter(QStringLiteral("PNG files (*.png *.PNG)"));
  // QFileSystemModel disables non-matching files by default. That is the
  // behavior seen in the macOS picker; make the Qt-backed dialog hide them
  // instead while retaining directory entries for navigation.
  if (auto* file_system_model = dialog.findChild<QFileSystemModel*>();
      file_system_model != nullptr) {
    file_system_model->setNameFilterDisables(false);
  }
  dialog.setFileMode(QFileDialog::ExistingFile);
  const QString previous_file = lastOpenFile();
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
  view_state_.hex_source = pnga::ui::qt::HexSource::kFile;
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
  view_state_.hex_source = source;
  if (hex_source_tabs_ != nullptr) {
    hex_source_tabs_->setSource(source);
  }
  updateHexSource();
}
