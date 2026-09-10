// Bounded canvas-pixel provenance for APNG inspection (WP-APNG-INSPECT
// contract C4). One canvas pixel's value is explained as a page-local DAG
// of blend/sample/clear/restore/carry operations. Numeric RGBA checkpoints
// reuse the compositor's integer formulas; the provenance walk itself is
// driven by the animation control records. Deep histories paginate through
// an explicit cursor instead of growing the working set.

#ifndef PNGA_ANALYSIS_ENGINE_CANVAS_PIXEL_QUERY_H
#define PNGA_ANALYSIS_ENGINE_CANVAS_PIXEL_QUERY_H

#include "pnga/analysis-engine/frame_analysis.h"

#include <pnga/trace-model/inspection_context.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class CanvasOperation {
  kFrameSample,  // the frame's own delivered pixel (FrameSample leaf)
  kBlendSource,  // APNG_BLEND_OP_SOURCE replaced the destination
  kBlendOver,    // APNG_BLEND_OP_OVER with the compositor integer formula
  kCarry,        // value carried from another (frame, stage) at this pixel
  kClear,        // initial canvas or dispose-to-background (no compressed
                 // source; fully transparent)
  kRestore,      // dispose-to-previous restored this frame's PreBlend
};

struct CanvasPixelNode {
  CanvasOperation operation = CanvasOperation::kClear;
  std::uint32_t frame = 0;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;
  std::array<std::uint8_t, 4> rgba{};
  std::vector<std::uint32_t> inputs;  // indices into this page's nodes;
                                      // always smaller than this node
};

struct CanvasPixelPending {
  std::uint32_t frame = 0;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;
};

struct CanvasPixelCursor {
  std::uint32_t version = 1;
  pnga::trace_model::InspectionTicket ticket;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t visited_steps = 0;  // cumulative expansions; checked
  // Deterministic continuation stack: pending[0] is the next state to
  // expand (depth-first order, source before destination history). At most
  // 1024 entries; empty pending cursors are rejected.
  std::vector<CanvasPixelPending> pending;
};

struct CanvasPixelRequest {
  FrameRequest document;
  pnga::trace_model::InspectionTicket ticket;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::optional<CanvasPixelCursor> cursor;
};

struct CanvasPixelResult {
  enum class Stop { kReady, kPartial, kCancelled, kError };

  Stop stop = Stop::kError;
  std::vector<CanvasPixelNode> nodes;  // closed page-local DAG; the last
                                       // node is the root's value
  std::optional<CanvasPixelCursor> next;
  std::string error;
};

// Page budgets (contract C5, fixed): at most 4096 nodes and 1024
// history-frame steps per page within a 4 MiB node memory bound. Deeper
// provenance continues through result.next.
CanvasPixelResult query_canvas_pixel(const CanvasPixelRequest& request,
                                     const CancellationToken* cancellation);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_CANVAS_PIXEL_QUERY_H
