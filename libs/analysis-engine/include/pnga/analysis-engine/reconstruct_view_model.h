#ifndef PNGA_ANALYSIS_ENGINE_RECONSTRUCT_VIEW_MODEL_H
#define PNGA_ANALYSIS_ENGINE_RECONSTRUCT_VIEW_MODEL_H

// WP-5U5A: immutable, Qt-free reconstruction explanation for one image
// coordinate. Formatting and widgets belong to later UI work.

#include <pnga/analysis-engine/stage_analysis.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class ReconstructStatus { kReady, kNoStage, kOutOfRange, kError };

struct ReconstructViewModel {
  ReconstructStatus status = ReconstructStatus::kError;
  std::string error;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t pass = 0;
  std::uint64_t pass_x = 0;
  std::uint64_t pass_y = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t sample_index = 0;
  std::uint64_t filtered_byte_offset = 0;
  std::uint64_t unfiltered_byte_offset = 0;
  std::uint64_t selected_byte = 0;
  std::vector<pnga::png_reconstruction::FilterTraceEvent> steps;
};

const char* reconstruct_status_text(ReconstructStatus status) noexcept;

ReconstructViewModel build_reconstruct_view(
    const StageSet& stages, std::uint64_t x, std::uint64_t y,
    std::uint64_t neighbor_radius = 2);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_RECONSTRUCT_VIEW_MODEL_H
