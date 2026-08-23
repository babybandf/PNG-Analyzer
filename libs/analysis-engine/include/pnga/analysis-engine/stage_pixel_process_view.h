#ifndef PNGA_ANALYSIS_ENGINE_STAGE_PIXEL_PROCESS_VIEW_H
#define PNGA_ANALYSIS_ENGINE_STAGE_PIXEL_PROCESS_VIEW_H

// WP-5U9: bounded, Qt-free facts for the central Pixels/Filtered/Defiltered
// views. The query only projects a fixed 5x3 neighborhood from one immutable
// StageSet; it never decodes, copies a complete stage, or owns GUI state.

#include <pnga/analysis-engine/stage_analysis.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class StagePixelProcessStage { kNative, kFiltered, kDefiltered };

enum class StagePixelProcessStatus {
  kReady,
  kNoStage,
  kOutOfRange,
  kUnavailable,
  kError,
};

const char* stage_pixel_process_status_text(
    StagePixelProcessStatus status) noexcept;

struct StagePixelProcessCell {
  bool in_bounds = false;
  bool current = false;
  bool exact_mapping = false;
  bool packed_shared = false;
  std::uint64_t image_x = 0;
  std::uint64_t image_y = 0;
  std::uint64_t pass_local_x = 0;
  std::uint64_t pass_local_y = 0;
  std::uint64_t pass_index = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t byte_offset = 0;
  std::uint64_t bit_offset = 0;
  std::uint64_t bit_length = 0;
  std::vector<std::uint8_t> bytes;
  std::vector<std::uint16_t> native_samples;
};

struct StagePixelProcessChannel {
  std::uint8_t source_index = 0;
  std::string name;
  std::vector<StagePixelProcessCell> cells;  // exactly 15 when ready
};

struct StagePixelProcessCalculation {
  std::uint8_t channel_index = 0;
  std::uint8_t lane = 0;
  std::uint64_t byte_offset = 0;
  bool has_raw = false;
  bool has_a = false;
  bool has_b = false;
  bool has_c = false;
  bool has_predictor = false;
  bool has_recon = false;
  std::uint8_t raw = 0;
  std::uint8_t a = 0;
  std::uint8_t b = 0;
  std::uint8_t c = 0;
  std::uint8_t predictor = 0;
  std::uint8_t recon = 0;
  bool boundary_zero_a = false;
  bool boundary_zero_b = false;
  bool boundary_zero_c = false;
};

struct StagePixelProcessView {
  StagePixelProcessStatus status = StagePixelProcessStatus::kError;
  std::string error;
  StagePixelProcessStage stage = StagePixelProcessStage::kNative;
  std::uint64_t image_x = 0;
  std::uint64_t image_y = 0;
  std::uint64_t pass_index = 0;
  std::uint64_t pass_local_x = 0;
  std::uint64_t pass_local_y = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t filter_byte_offset = 0;
  std::uint8_t filter_byte = 0;
  pnga::png_reconstruction::FilterType filter =
      pnga::png_reconstruction::FilterType::kNone;
  std::vector<StagePixelProcessChannel> channels;
  std::vector<StagePixelProcessCalculation> calculations;
};

StagePixelProcessView build_stage_pixel_process_view(
    const StageSet& stages, StagePixelProcessStage stage, std::uint64_t x,
    std::uint64_t y);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STAGE_PIXEL_PROCESS_VIEW_H
