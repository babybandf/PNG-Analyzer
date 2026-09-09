#include "pnga/analysis-engine/analysis_target.h"

#include <pnga/png-format/virtual_frame_stream.h>

#include <utility>

namespace pnga::analysis_engine {

TargetResult make_frame_target(const FrameRequest& request) {
  TargetResult result;
  if (!request.source || !request.index ||
      request.ordinal >= request.index->frames.size()) {
    result.error = "frame ordinal is outside the verified prefix";
    return result;
  }
  auto stream = pnga::png_format::make_frame_stream(
      request.source, request.index, request.ordinal);
  if (!stream) {
    result.error = "frame stream is unavailable";
    return result;
  }
  const auto& record = request.index->frames[request.ordinal];
  auto target = std::make_shared<AnalysisTarget>();
  target->key.generation = request.generation;
  target->key.identity = pnga::trace_model::ImageIdentity{
      pnga::trace_model::AnimationFrame{request.ordinal}};
  target->source = request.source;
  target->stream = std::move(stream);
  target->header = request.canvas_header;
  target->header.width = record.control.width;
  target->header.height = record.control.height;
  target->delivery = request.delivery;
  target->control = record.control;
  result.target = std::move(target);
  return result;
}

std::optional<LocalPoint> frame_local_point(
    const pnga::png_format::FrameControl& rect,
    std::uint64_t global_x, std::uint64_t global_y) noexcept {
  if (rect.width == 0 || rect.height == 0) {
    return std::nullopt;
  }
  const std::uint64_t origin_x = rect.x;
  const std::uint64_t origin_y = rect.y;
  if (global_x < origin_x || global_y < origin_y) {
    return std::nullopt;
  }
  const std::uint64_t local_x = global_x - origin_x;
  const std::uint64_t local_y = global_y - origin_y;
  if (local_x >= rect.width || local_y >= rect.height) {
    return std::nullopt;
  }
  return LocalPoint{local_x, local_y};
}

CoordinateSummary query_frame_coordinate(
    const FrameStageSet& frame,
    const pnga::trace_model::Selection& global_selection) {
  if (!global_selection.image.has_value()) {
    return query_coordinate(frame.stages, global_selection);
  }
  if (!(global_selection.image->identity == frame.identity)) {
    CoordinateSummary out;
    out.selection = global_selection;
    out.status = CoordinateQueryStatus::kNotApplicable;
    out.error = "selection identity does not match the analyzed frame";
    return out;
  }
  const auto& image = *global_selection.image;
  const std::uint64_t origin_x = frame.control.x;
  const std::uint64_t origin_y = frame.control.y;
  if (image.x < origin_x || image.y < origin_y) {
    CoordinateSummary out;
    out.selection = global_selection;
    out.status = CoordinateQueryStatus::kOutOfRange;
    out.error = "selection is outside the frame rectangle";
    return out;
  }

  // The pass-local kernel resolves static-identity coordinates only, so
  // the local query runs with a neutral identity; the frame identity is
  // restored in every returned coordinate below.
  pnga::trace_model::Selection local = global_selection;
  local.image->identity = pnga::trace_model::StaticImage{};
  local.image->x = image.x - origin_x;
  local.image->y = image.y - origin_y;

  CoordinateSummary summary = query_coordinate(frame.stages, local);
  auto restore = [identity = frame.identity, origin_x, origin_y](
                     pnga::trace_model::ImageCoordinate& coordinate) {
    coordinate.identity = identity;
    coordinate.x += origin_x;
    coordinate.y += origin_y;
  };
  if (summary.selection.image.has_value()) {
    restore(*summary.selection.image);
  }
  if (summary.image.has_value()) {
    restore(*summary.image);
  }
  return summary;
}

}  // namespace pnga::analysis_engine
