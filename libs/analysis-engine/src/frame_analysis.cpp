#include "pnga/analysis-engine/frame_analysis.h"

#include <pnga/png-format/virtual_frame_stream.h>

#include <cstddef>
#include <utility>

namespace pnga::analysis_engine {
namespace {

std::uint16_t big_endian_u16(const std::byte* data) {
  return static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(data[0]) << 8) |
      std::to_integer<std::uint16_t>(data[1]));
}

}  // namespace

pnga::png_reconstruction::DeliveryContext delivery_context_from(
    const pnga::png_format::AnimationIndex& index,
    const pnga::png_reconstruction::ImageHeader& canvas_header) {
  pnga::png_reconstruction::DeliveryContext context;
  if (canvas_header.color_type == 3) {
    if (index.palette_bytes.empty() || index.palette_bytes.size() % 3 != 0 ||
        index.palette_bytes.size() > 768) {
      return context;
    }
    for (std::size_t i = 0; i + 2 < index.palette_bytes.size(); i += 3) {
      context.palette.push_back(
          {std::to_integer<std::uint8_t>(index.palette_bytes[i]),
           std::to_integer<std::uint8_t>(index.palette_bytes[i + 1]),
           std::to_integer<std::uint8_t>(index.palette_bytes[i + 2])});
    }
    context.palette_alpha.reserve(index.transparency_bytes.size());
    for (const auto alpha : index.transparency_bytes) {
      context.palette_alpha.push_back(std::to_integer<std::uint8_t>(alpha));
    }
    return context;
  }
  if (canvas_header.color_type == 0) {
    if (index.transparency_bytes.size() == 2) {
      context.transparent_gray =
          big_endian_u16(index.transparency_bytes.data());
    }
    return context;
  }
  if (canvas_header.color_type == 2) {
    if (index.transparency_bytes.size() == 6) {
      context.transparent_rgb = {big_endian_u16(index.transparency_bytes.data()),
                                 big_endian_u16(index.transparency_bytes.data() + 2),
                                 big_endian_u16(index.transparency_bytes.data() + 4)};
    }
    return context;
  }
  return context;
}

FrameResult analyze_frame(const FrameRequest& request,
                          const CancellationToken* cancellation) {
  FrameResult result;
  result.generation = request.generation;
  result.request_serial = request.request_serial;
  result.identity =
      pnga::trace_model::ImageIdentity{
          pnga::trace_model::AnimationFrame{request.ordinal}};

  auto fail = [&result](FrameResult::Stop stop, const char* message) {
    result.stop = stop;
    result.error = message;
    result.frame.reset();
    return result;
  };
  const auto cancelled = [cancellation]() {
    return cancellation != nullptr && cancellation->cancelled();
  };
  if (cancelled()) {
    return fail(FrameResult::Stop::kCancelled, "frame analysis cancelled");
  }
  if (!request.source || !request.index ||
      request.ordinal >= request.index->frames.size()) {
    return fail(FrameResult::Stop::kError,
                "frame ordinal is outside the verified prefix");
  }

  const auto stream = pnga::png_format::make_frame_stream(
      request.source, request.index, request.ordinal);
  if (!stream) {
    return fail(FrameResult::Stop::kError, "frame stream is unavailable");
  }

  const auto& record = request.index->frames[request.ordinal];
  auto frame_header = request.canvas_header;
  frame_header.width = record.control.width;
  frame_header.height = record.control.height;
  const StageSet stages =
      analyze_stages(*stream, frame_header, request.limits, cancellation);
  if (stages.stop == StageStop::kCancelled || cancelled()) {
    return fail(FrameResult::Stop::kCancelled, "frame analysis cancelled");
  }
  if (stages.stop == StageStop::kBudget) {
    return fail(FrameResult::Stop::kPartial, stages.error.c_str());
  }
  if (!stages.success) {
    return fail(FrameResult::Stop::kError, stages.error.c_str());
  }

  const auto delivered = pnga::png_reconstruction::deliver_rgba8(
      stages.native, request.delivery, request.limits.max_working_bytes,
      cancelled);
  if (delivered.cancelled || cancelled()) {
    return fail(FrameResult::Stop::kCancelled, "frame analysis cancelled");
  }
  if (!delivered.success) {
    return fail(FrameResult::Stop::kError, delivered.error.c_str());
  }

  auto frame = std::make_shared<FrameStageSet>();
  frame->identity = result.identity;
  frame->control = record.control;
  frame->stages = stages;
  frame->delivered = delivered.image;
  result.stop = FrameResult::Stop::kReady;
  result.error.clear();
  result.frame = std::move(frame);
  return result;
}

}  // namespace pnga::analysis_engine
