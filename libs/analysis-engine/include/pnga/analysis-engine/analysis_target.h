// Immutable analysis targets for per-frame inspection (WP-APNG-INSPECT
// contract C1). An AnalysisTarget owns the format context of one APNG
// frame; frame-local coordinate mapping and frame-scoped coordinate
// queries route canvas-global requests through the pass-local kernel.

#ifndef PNGA_ANALYSIS_ENGINE_ANALYSIS_TARGET_H
#define PNGA_ANALYSIS_ENGINE_ANALYSIS_TARGET_H

#include "pnga/analysis-engine/coordinate_query.h"
#include "pnga/analysis-engine/frame_analysis.h"

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/virtual_compressed_stream.h>
#include <pnga/png-reconstruction/rgba_delivery.h>
#include <pnga/png-reconstruction/scanline_layout.h>
#include <pnga/trace-model/inspection_context.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace pnga::analysis_engine {

// A point in frame-local integer coordinates (relative to the frame
// rectangle origin on the canvas).
struct LocalPoint {
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  bool operator==(const LocalPoint&) const = default;
};

// Immutable analysis context for one APNG frame: the virtual compressed
// stream restricted to the frame payload plus the format context needed to
// analyze it. Instances are passed as shared_ptr<const AnalysisTarget> and
// never mutated after construction.
struct AnalysisTarget {
  pnga::trace_model::AnalysisKey key;
  std::shared_ptr<const pnga::io::IByteSource> source;
  std::shared_ptr<const pnga::png_format::IVirtualCompressedStream> stream;
  pnga::png_reconstruction::ImageHeader header;
  pnga::png_reconstruction::DeliveryContext delivery;
  std::optional<pnga::png_format::FrameControl> control;
};

struct TargetResult {
  std::shared_ptr<const AnalysisTarget> target;
  std::string error;
};

// Builds the analysis target for the frame identified by `request` in the
// document's verified prefix. Implemented with the frame stream factory;
// the returned target borrows nothing from `request` beyond shared
// ownership of source, index-derived stream and delivery context.
TargetResult make_frame_target(const FrameRequest& request);

// Maps a canvas-global point into the frame rectangle. Returns nullopt
// when the rectangle is degenerate or the point lies outside it. All
// comparisons are checked; extreme coordinates must not wrap.
std::optional<LocalPoint> frame_local_point(
    const pnga::png_format::FrameControl& rect,
    std::uint64_t global_x, std::uint64_t global_y) noexcept;

// Resolves a canvas-global selection against an already analyzed frame.
// The selection's image identity must match the frame identity
// (kNotApplicable otherwise) and its coordinates must fall inside the
// frame rectangle (kOutOfRange otherwise). Pass/channel handling and
// byte/bit offsets are delegated to query_coordinate on the frame-local
// stage data; output image coordinates are restored to canvas-global
// while pass-local fields (local_x, row_in_pass, stream_row) keep their
// frame-local pass semantics.
CoordinateSummary query_frame_coordinate(
    const FrameStageSet& frame,
    const pnga::trace_model::Selection& global_selection);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_ANALYSIS_TARGET_H
