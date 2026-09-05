// WP-5U15: selection/navigation bodies moved verbatim from main_window.cpp,
// including their file-local constants and helpers. The controller never maps
// files, owns no worker and creates no trace orchestrator; trace submission
// and selection-priority replays are delegated through the callbacks.

#include "selection_navigation_controller.h"

#include <pnga/ui/qt/chunk_detail_panel.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_data_source.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>

#include <QCheckBox>
#include <QColor>
#include <QImage>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeView>
#include <QVector>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr int kChunkPanelOrigin = 1;
constexpr int kImagePanelOrigin = 2;
constexpr int kHexPanelOrigin = 3;
constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;

}  // namespace

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

SelectionNavigationController::SelectionNavigationController(
    MainWindowWidgets widgets, SelectionNavigationCallbacks callbacks,
    QObject* parent, pnga::ui::qt::SelectionViewState* shared_view_state,
    pnga::ui::qt::CompressionSelectionStore* compression_store)
    : QObject(parent),
      w_(widgets),
      callbacks_(std::move(callbacks)),
      internal_view_state_(),
      view_state_(shared_view_state != nullptr ? *shared_view_state
                                               : internal_view_state_),
      compression_store_(compression_store) {
  default_pixel_status_ = QStringLiteral("No image");
  if (compression_store_ != nullptr) {
    // The controller is the single receiver of the store's navigation
    // requests; the store owns Current/Manual state and history.
    connect(compression_store_,
            &pnga::ui::qt::CompressionSelectionStore::navigationRequested,
            this, &SelectionNavigationController::applyCompressionNavigation);
  }
}

pnga::ui::qt::SelectionViewState& SelectionNavigationController::viewState()
    noexcept {
  return view_state_;
}

const pnga::ui::qt::SelectionViewState&
SelectionNavigationController::viewState() const noexcept {
  return view_state_;
}

std::uint64_t SelectionNavigationController::chunkSelectionSerial()
    const noexcept {
  return chunk_selection_serial_;
}

void SelectionNavigationController::setDocument(
    std::uint64_t generation, std::shared_ptr<const pnga::io::IByteSource> source,
    const pnga::png_format::ChunkIndex* index,
    pnga::analysis_engine::QueryCoordinator* query) {
  generation_ = generation;
  source_ = std::move(source);
  index_ = index;
  query_ = query;
  stage_set_.reset();
  view_state_.set_document_generation(generation);
  last_applied_navigation_serial_ = 0;
  if (compression_store_ != nullptr) {
    compression_store_->resetGeneration(generation);
  }
}

void SelectionNavigationController::clearDocument(std::uint64_t generation) {
  generation_ = generation;
  source_.reset();
  index_ = nullptr;
  query_ = nullptr;
  stage_set_.reset();
  view_state_.set_document_generation(generation);
  view_state_.clear_hover();
  view_state_.clear_locked();
  last_applied_navigation_serial_ = 0;
  if (compression_store_ != nullptr) {
    compression_store_->resetGeneration(generation);
  }
}

void SelectionNavigationController::applyCompressionNavigation(
    const pnga::trace_model::CompressionNavigationTarget& target) {
  if (w_.hex == nullptr) {
    return;
  }
  if (target.generation != generation_ || target.request_serial == 0 ||
      target.request_serial == last_applied_navigation_serial_) {
    return;  // stale generation or an already-applied serial (loop guard).
  }
  last_applied_navigation_serial_ = target.request_serial;
  const auto highlight_spans =
      [this](const std::vector<pnga::trace_model::FileByteRange>& spans) {
        if (spans.empty()) {
          return;
        }
        std::vector<pnga::ui::qt::HexHighlightSpan> highlights;
        highlights.reserve(spans.size());
        for (const auto& span : spans) {
          if (!span.valid() || span.empty()) {
            continue;
          }
          highlights.push_back(
              {span.begin.value, span.end - span.begin,
               QColor(0x42, 0xA5, 0xF5)});
        }
        w_.hex->setHighlight(std::move(highlights));
        w_.hex->navigateTo(spans.front().begin.value);
      };
  std::visit(
      [&](const auto& range) {
        using Range = std::decay_t<decltype(range)>;
        if constexpr (std::is_same_v<Range,
                                     pnga::trace_model::InflatedByteRange>) {
          // Inflated output routes through the existing Inflated source.
          setHexSource(pnga::ui::qt::HexSource::kInflated);
          w_.hex->navigateTo(range.begin.value);
        } else if constexpr (std::is_same_v<Range,
                                            pnga::trace_model::FileByteRange>) {
          setHexSource(pnga::ui::qt::HexSource::kFile);
          if (!target.physical_spans.empty()) {
            highlight_spans(target.physical_spans);
          } else {
            highlight_spans({range});
          }
        } else {
          // Zlib byte/bit and DEFLATE bit ranges show the compressed input
          // through every mapped physical file span; no bit normalization
          // happens here and no span is dropped.
          setHexSource(pnga::ui::qt::HexSource::kFile);
          highlight_spans(target.physical_spans);
        }
      },
      target.logical_range);
}

void SelectionNavigationController::setCompressionCurrent(
    const pnga::trace_model::CompressionCurrentMapping& mapping) {
  if (mapping.generation != generation_) {
    return;  // stale document context.
  }
  if (compression_store_ != nullptr) {
    compression_store_->setCurrent(mapping);
  }
}

void SelectionNavigationController::setQueryCoordinator(
    pnga::analysis_engine::QueryCoordinator* query) {
  query_ = query;
}

void SelectionNavigationController::onStageSetPublished(
    const std::shared_ptr<const pnga::analysis_engine::StageSet>& stages) {
  stage_set_ = stages;
  w_.inspector->setStageSet(stages);
  w_.pixel_view->setStageSet(stages);
  w_.filtered_view->setStageSet(stages);
  w_.defiltered_view->setStageSet(stages);
  updateHexSource();
  // updateHexSource() clears the hex highlight; restore the selected chunk's
  // physical highlight (e.g. the default IHDR) so the hex view keeps showing
  // it after the stage worker refreshes the source.
  const QModelIndex current_chunk = w_.tree->selectionModel()->currentIndex();
  if (current_chunk.isValid() &&
      view_state_.hex_source == pnga::ui::qt::HexSource::kFile) {
    applyChunkHexHighlight(model_->chunkAt(current_chunk.row()));
  }
}

void SelectionNavigationController::replaceChunkModel(
    const pnga::png_format::ChunkIndex* index) {
  ++chunk_selection_serial_;
  if (w_.chunk_detail != nullptr) {
    w_.chunk_detail->clear();
  }
  if (model_ != nullptr) {
    delete model_;
  }
  model_ = new pnga::ui::qt::ChunkModel(index, this);
  w_.tree->setModel(model_);
  // Document open re-derives column widths from content; while the document
  // stays open, manual widths survive selection changes and navigation.
  for (int column = 0; column < model_->columnCount(); ++column) {
    w_.tree->resizeColumnToContents(column);
  }
  // setModel() replaces the selection model; reconnect to the new one.
  connect(w_.tree->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &SelectionNavigationController::onChunkSelectionChanged);
  updateHexSource();

  if (model_->rowCount() > 0) {
    w_.tree->selectionModel()->setCurrentIndex(
        model_->index(0, 0),
        QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  }
}

void SelectionNavigationController::setDefaultPixelStatus(const QString& text) {
  default_pixel_status_ = text;
  w_.pixel_label->setText(default_pixel_status_);
}

void SelectionNavigationController::onChunkSelectionChanged(
    const QModelIndex& current, const QModelIndex& /*previous*/) {
  const std::uint64_t selection_serial = ++chunk_selection_serial_;
  if (!current.isValid()) {
    w_.hex->clearHighlight();
    if (w_.chunk_detail != nullptr) {
      w_.chunk_detail->clear();
    }
    return;
  }
  const auto& node = model_->chunkAt(current.row());

  // Chunk offsets are physical file offsets; never apply them to a virtual
  // IDAT or reconstructed stage address space.
  view_state_.hex_source = pnga::ui::qt::HexSource::kFile;
  updateHexSource();

  if (w_.chunk_detail != nullptr && source_ != nullptr) {
    w_.chunk_detail->setLoading();
    if (callbacks_.request_chunk_detail) {
      callbacks_.request_chunk_detail(node, selection_serial);
    }
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
  w_.bus->publishMerged(kChunkPanelOrigin, generation_, sel);
}

void SelectionNavigationController::applyChunkHexHighlight(
    const pnga::png_format::ChunkNode& node) {
  if (w_.hex == nullptr) {
    return;
  }
  std::vector<pnga::ui::qt::HexHighlightSpan> spans;
  spans.push_back({node.header_offset, kHeaderSpanLength,
                   QColor(0x9E, 0x9E, 0x9E)});  // header: gray
  spans.push_back({node.data_offset, node.data_length,
                   QColor(0x42, 0xA5, 0xF5)});  // data: blue
  spans.push_back({node.crc_offset, kCrcSpanLength,
                   QColor(0x66, 0xBB, 0x6A)});  // CRC: green
  w_.hex->setHighlight(std::move(spans));
  w_.hex->navigateTo(node.header_offset);
}

void SelectionNavigationController::onPixelSelected(int x, int y) {
  {
    const QSignalBlocker x_blocker(w_.x_spin);
    const QSignalBlocker y_blocker(w_.y_spin);
    const QSignalBlocker lock_blocker(w_.lock_check);
    w_.x_spin->setValue(x);
    w_.y_spin->setValue(y);
    w_.lock_check->setChecked(true);
  }
  w_.inspector->onPixelSelected(static_cast<std::uint64_t>(x),
                                static_cast<std::uint64_t>(y));
  if (query_ != nullptr && query_->has_index() && stage_set_ != nullptr) {
    // Map the clicked pixel to its stream row and issue a selection-priority
    // replay if the row's data is not materialized yet.
    const auto& layout = query_->anchors().layout;
    const auto row = pnga::analysis_engine::stream_row_for_pixel(
        layout, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y));
    if (row.has_value()) {
      callbacks_.request_scanline(*row);
    }
  }
  pnga::trace_model::Selection sel;
  sel.image = pnga::trace_model::ImageCoordinate{
      0, 0, 0, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y)};
  sel.stage = pnga::trace_model::Stage::kDelivered;
  view_state_.set_locked(*sel.image);
  w_.image_view->setLockedPixel(QPoint(x, y));
  w_.pixel_view->setCoordinate(static_cast<std::uint64_t>(x),
                               static_cast<std::uint64_t>(y));
  w_.filtered_view->setCoordinate(static_cast<std::uint64_t>(x),
                                  static_cast<std::uint64_t>(y));
  w_.defiltered_view->setCoordinate(static_cast<std::uint64_t>(x),
                                    static_cast<std::uint64_t>(y));
  w_.bus->publishMerged(kImagePanelOrigin, generation_, sel);
  if (callbacks_.request_trace) {
    callbacks_.request_trace(
        pnga::trace_model::ImageCoordinate{0, 0, 0,
                                           static_cast<std::uint64_t>(x),
                                           static_cast<std::uint64_t>(y)});
  }
  setPixelStatus(x, y);
}

void SelectionNavigationController::onPixelHovered(int x, int y) {
  const pnga::trace_model::ImageCoordinate coordinate{
      0, 0, 0, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y)};
  view_state_.set_hover(coordinate);
  setPixelStatus(x, y);
}

void SelectionNavigationController::onPixelHoverLeft() {
  view_state_.clear_hover();
  restorePixelStatus();
}

void SelectionNavigationController::publishLockedCoordinate() {
  const pnga::trace_model::ImageCoordinate coordinate{
      0, 0, 0, static_cast<std::uint64_t>(w_.x_spin->value()),
      static_cast<std::uint64_t>(w_.y_spin->value())};
  if (!view_state_.set_locked(coordinate)) {
    return;
  }
  w_.image_view->setLockedPixel(
      QPoint(static_cast<int>(coordinate.x), static_cast<int>(coordinate.y)));
  w_.pixel_view->setCoordinate(coordinate.x, coordinate.y);
  w_.filtered_view->setCoordinate(coordinate.x, coordinate.y);
  w_.defiltered_view->setCoordinate(coordinate.x, coordinate.y);
  w_.inspector->onPixelSelected(coordinate.x, coordinate.y);
  pnga::trace_model::Selection update;
  update.image = coordinate;
  update.stage = pnga::trace_model::Stage::kDelivered;
  w_.bus->publishMerged(kImagePanelOrigin, generation_, update);
  if (callbacks_.request_trace) {
    callbacks_.request_trace(coordinate);
  }
}

void SelectionNavigationController::clearLockedCoordinate() {
  view_state_.clear_locked();
  w_.image_view->clearLockedPixel();
  {
    const QSignalBlocker lock_blocker(w_.lock_check);
    w_.lock_check->setChecked(false);
  }
  if (view_state_.hover.has_value()) {
    setPixelStatus(static_cast<int>(view_state_.hover->x),
                   static_cast<int>(view_state_.hover->y));
  } else {
    restorePixelStatus();
  }
  pnga::trace_model::Selection current = w_.bus->current();
  current.image.reset();
  if (current.stage == pnga::trace_model::Stage::kDelivered) {
    current.stage = pnga::trace_model::Stage::kUnknown;
  }
  w_.bus->publish(kImagePanelOrigin, generation_, current);
}

void SelectionNavigationController::nudgeLockedCoordinate(int dx, int dy) {
  if (!view_state_.locked.has_value() || w_.image_view->image().isNull()) {
    return;
  }
  const QImage image = w_.image_view->image();
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

void SelectionNavigationController::toggleNumericBase() {
  const bool hexadecimal =
      view_state_.numeric_base != pnga::ui::qt::NumericBase::kHexadecimal;
  view_state_.numeric_base =
      hexadecimal ? pnga::ui::qt::NumericBase::kHexadecimal
                  : pnga::ui::qt::NumericBase::kDecimal;
  w_.inspector->setNumericBase(hexadecimal);
  w_.pixel_view->setNumericBase(hexadecimal);
  w_.filtered_view->setNumericBase(hexadecimal);
  w_.defiltered_view->setNumericBase(hexadecimal);
  updateNumericBaseButton();
}

void SelectionNavigationController::setHexSource(pnga::ui::qt::HexSource source) {
  view_state_.hex_source = source;
  if (w_.hex_source_tabs != nullptr) {
    w_.hex_source_tabs->setSource(source);
  }
  updateHexSource();
}

void SelectionNavigationController::onHexSourceTabChanged(
    pnga::ui::qt::HexSource source) {
  view_state_.hex_source = source;
  updateHexSource();
}

void SelectionNavigationController::refreshHexSource() {
  updateHexSource();
}

void SelectionNavigationController::updateHexSource() {
  if (w_.hex_source_tabs != nullptr) {
    w_.hex_source_tabs->setSource(view_state_.hex_source);
  }
  if (source_ == nullptr) {
    w_.hex->setSource(nullptr);
    return;
  }
  if (view_state_.hex_source == pnga::ui::qt::HexSource::kIdatStream) {
    const pnga::png_format::VirtualIDATStream stream(*index_);
    w_.hex->setSource(pnga::ui::qt::make_idat_hex_source(source_, stream));
  } else if (view_state_.hex_source == pnga::ui::qt::HexSource::kInflated) {
    w_.hex->setSource(pnga::ui::qt::make_inflated_hex_source(stage_set_));
  } else if (view_state_.hex_source ==
             pnga::ui::qt::HexSource::kDefiltered) {
    w_.hex->setSource(pnga::ui::qt::make_defiltered_hex_source(stage_set_));
  } else {
    w_.hex->setSource(pnga::ui::qt::make_file_hex_source(source_));
  }
  w_.hex->clearHighlight();
}

void SelectionNavigationController::updateNumericBaseButton() {
  if (w_.base_button == nullptr) {
    return;
  }
  const bool hexadecimal =
      view_state_.numeric_base == pnga::ui::qt::NumericBase::kHexadecimal;
  const QString target = hexadecimal ? QStringLiteral("DEC")
                                     : QStringLiteral("HEX");
  w_.base_button->setText(target);
  w_.base_button->setToolTip(QStringLiteral("Switch to %1").arg(target));
}

void SelectionNavigationController::setPixelStatus(int x, int y) {
  const auto rgba = w_.image_view->rgbaAt(x, y);
  if (!rgba.has_value()) {
    restorePixelStatus();
    return;
  }
  w_.pixel_label->setText(
      QStringLiteral("pixel (%1, %2) RGBA(%3, %4, %5, %6)")
          .arg(x)
          .arg(y)
          .arg((*rgba)[0])
          .arg((*rgba)[1])
          .arg((*rgba)[2])
          .arg((*rgba)[3]));
}

void SelectionNavigationController::restorePixelStatus() {
  // Only re-display the locked pixel's RGBA while an image can actually
  // provide it; a committed lock without a delivered image (standalone
  // controller tests, transient decode failure) falls back to the default
  // status instead of recursing between here and setPixelStatus().
  if (view_state_.locked.has_value() && !w_.image_view->image().isNull()) {
    setPixelStatus(static_cast<int>(view_state_.locked->x),
                   static_cast<int>(view_state_.locked->y));
    return;
  }
  w_.pixel_label->setText(default_pixel_status_);
}
