#ifndef PNG_ANALYZER_GUI_SELECTION_NAVIGATION_CONTROLLER_H
#define PNG_ANALYZER_GUI_SELECTION_NAVIGATION_CONTROLLER_H

// WP-5U15: single owner of the selection/navigation presentation: X/Y lock,
// SelectionBus publication, Chunk/Pixel/Hex navigation, hex source state and
// the no-replay hover rule. Bodies moved verbatim from the facade; trace
// submission and query replays are delegated through the callbacks so this
// unit never owns the trace orchestrator.

#include "main_window_ui.h"

#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_compressed_stream.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/selection_view_state.h>

#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace pnga::ui::qt {
class ChunkModel;
class CompressionSelectionStore;
}

struct SelectionNavigationCallbacks final {
  std::function<void(const pnga::trace_model::ImageCoordinate&)> request_trace;
  std::function<void(std::uint64_t)> request_scanline;
  // Invoked for every valid chunk selection with the controller's selection
  // serial; the facade routes it to DocumentSession::requestChunkDetail.
  std::function<void(const pnga::png_format::ChunkNode&, std::uint64_t)>
      request_chunk_detail;
};

class SelectionNavigationController final : public QObject {
  Q_OBJECT
 public:
  // `shared_view_state` may be null (standalone tests); the controller then
  // uses its own instance. The product facade shares the workspace-owned
  // state so Reset Layout and selection editing observe one object.
  // `compression_store` may be null; when present the controller keeps its
  // generation in step with the document and is the single receiver of the
  // store's navigation requests (WP-5U12B).
  SelectionNavigationController(MainWindowWidgets widgets,
                                SelectionNavigationCallbacks callbacks,
                                QObject* parent = nullptr,
                                pnga::ui::qt::SelectionViewState* shared_view_state = nullptr,
                                pnga::ui::qt::CompressionSelectionStore* compression_store = nullptr);

  void setDocument(std::uint64_t generation,
                   std::shared_ptr<const pnga::io::IByteSource> source,
                   const pnga::png_format::ChunkIndex* index,
                   pnga::analysis_engine::QueryCoordinator* query);
  void clearDocument(std::uint64_t generation);
  void replaceChunkModel(const pnga::png_format::ChunkIndex* index);
  void setDefaultPixelStatus(const QString& text);
  void setQueryCoordinator(pnga::analysis_engine::QueryCoordinator* query);
  void onStageSetPublished(
      const std::shared_ptr<const pnga::analysis_engine::StageSet>& stages);
  void refreshHexSource();
  void setImageIdentity(
      const pnga::trace_model::ImageIdentity& identity) noexcept;
  void setAnimationView(pnga::ui::qt::DeliveredImageView* view, pnga::trace_model::Stage stage,
                        std::uint32_t origin_x, std::uint32_t origin_y);
  void onAnimationPixelSelected(int x, int y);
  // Frame-local hover of the mounted animation view, converted to the
  // canvas-global hover status (C6; animation views emit frame-local
  // coordinates).
  void onAnimationFrameHovered(int x, int y);
  void setAnimationFrameStream(
      std::shared_ptr<const pnga::png_format::IVirtualCompressedStream> stream);

  pnga::ui::qt::SelectionViewState& viewState() noexcept;
  const pnga::ui::qt::SelectionViewState& viewState() const noexcept;
  std::uint64_t chunkSelectionSerial() const noexcept;

  // WP-5U12B typed Compression entry points. Both check the document
  // generation first and never submit Deep Trace work. Navigation routes the
  // target's variant and every physical span to the Hex/Inflated views and
  // suppresses a serial it already applied; Current is published through the
  // shared store so Manual Selection survives.
  void applyCompressionNavigation(
      const pnga::trace_model::CompressionNavigationTarget& target);
  void setCompressionCurrent(
      const pnga::trace_model::CompressionCurrentMapping& mapping);

 public slots:
  void onChunkSelectionChanged(const QModelIndex& current,
                               const QModelIndex& previous);
  void onPixelSelected(int x, int y);
  void onPixelHovered(int x, int y);
  void onPixelHoverLeft();
  void publishLockedCoordinate();
  void clearLockedCoordinate();
  void nudgeLockedCoordinate(int dx, int dy);
  void toggleNumericBase();
  void setHexSource(pnga::ui::qt::HexSource source);
  void onHexSourceTabChanged(pnga::ui::qt::HexSource source);

 private:
  void applyChunkHexHighlight(const pnga::png_format::ChunkNode& node);
  void onSelectionChanged(
      int origin, const pnga::trace_model::Selection& selection);
  void updateHexSource();
  void updateNumericBaseButton();
  void setPixelStatus(int x, int y);
  void restorePixelStatus();
  // The active presentation view for hover/lock status (C6/D3).
  pnga::ui::qt::DeliveredImageView* activePixelView() const noexcept;

  MainWindowWidgets w_;
  SelectionNavigationCallbacks callbacks_;
  pnga::ui::qt::SelectionViewState internal_view_state_;
  pnga::ui::qt::SelectionViewState& view_state_;
  pnga::ui::qt::ChunkModel* model_ = nullptr;
  std::shared_ptr<const pnga::io::IByteSource> source_;
  const pnga::png_format::ChunkIndex* index_ = nullptr;
  pnga::analysis_engine::QueryCoordinator* query_ = nullptr;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stage_set_;
  std::shared_ptr<const pnga::png_format::IVirtualCompressedStream>
      frame_stream_;
  pnga::trace_model::ImageIdentity image_identity_ =
      pnga::trace_model::StaticImage{};
  std::uint64_t generation_ = 0;
  std::uint64_t chunk_selection_serial_ = 0;
  std::uint64_t last_applied_navigation_serial_ = 0;
  pnga::ui::qt::CompressionSelectionStore* compression_store_ = nullptr;
  QString default_pixel_status_;
  QPointer<pnga::ui::qt::DeliveredImageView> animation_view_;
  pnga::trace_model::Stage animation_stage_ = pnga::trace_model::Stage::kUnknown;
  std::uint32_t animation_origin_x_ = 0, animation_origin_y_ = 0;
};

// Absolute inflated byte offset of a pixel's sample within its scanline.
// Shared with the trace controller's request path.
std::optional<std::uint64_t> filtered_output_offset_for_pixel(
    const pnga::analysis_engine::ScanlineAnchorIndexResult& anchors,
    const pnga::trace_model::ImageCoordinate& coordinate,
    std::uint64_t stream_row);

#endif  // PNG_ANALYZER_GUI_SELECTION_NAVIGATION_CONTROLLER_H
