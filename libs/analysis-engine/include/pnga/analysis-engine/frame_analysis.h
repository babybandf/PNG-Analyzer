#ifndef PNGA_ANALYSIS_ENGINE_FRAME_ANALYSIS_H
#define PNGA_ANALYSIS_ENGINE_FRAME_ANALYSIS_H

#include "pnga/analysis-engine/job_scheduler.h"
#include "pnga/analysis-engine/stage_analysis.h"

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-reconstruction/rgba_delivery.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <memory>
#include <string>

namespace pnga::analysis_engine {

struct FrameRequest {
  std::uint64_t generation = 0;
  std::uint64_t request_serial = 0;
  std::uint32_t ordinal = 0;
  std::shared_ptr<const pnga::io::IByteSource> source;
  std::shared_ptr<const pnga::png_format::AnimationIndex> index;
  pnga::png_reconstruction::ImageHeader canvas_header;
  pnga::png_reconstruction::DeliveryContext delivery;
  DecodeLimits limits;
};

struct FrameStageSet {
  pnga::trace_model::ImageIdentity identity =
      pnga::trace_model::AnimationFrame{0};
  pnga::png_format::FrameControl control;
  StageSet stages;
  pnga::png_reconstruction::RgbaImage delivered;
};

struct FrameResult {
  enum class Stop { kReady, kPartial, kCancelled, kError };

  std::uint64_t generation = 0;
  std::uint64_t request_serial = 0;
  pnga::trace_model::ImageIdentity identity =
      pnga::trace_model::AnimationFrame{0};
  Stop stop = Stop::kError;
  std::string error;
  std::shared_ptr<const FrameStageSet> frame;
};

// Converts the PLTE/tRNS bytes retained by the animation index into the
// delivery context analyze_frame applies to every frame of the document.
// Interpretation follows the canvas header's color type: palette indices
// for type 3, a 2-byte gray sample for type 0 and a 6-byte RGB triple for
// type 2, all compared at the original bit depth. Malformed bytes yield an
// empty context so delivery fails with its stable palette error instead of
// guessing.
pnga::png_reconstruction::DeliveryContext delivery_context_from(
    const pnga::png_format::AnimationIndex& index,
    const pnga::png_reconstruction::ImageHeader& canvas_header);

FrameResult analyze_frame(const FrameRequest& request,
                          const CancellationToken* cancellation);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_FRAME_ANALYSIS_H
