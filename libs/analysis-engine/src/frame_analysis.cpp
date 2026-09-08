#include "pnga/analysis-engine/frame_analysis.h"

#include <pnga/png-format/virtual_frame_stream.h>

#include <utility>

namespace pnga::analysis_engine {

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
