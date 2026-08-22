#ifndef PNGA_ANALYSIS_ENGINE_COORDINATE_QUERY_H
#define PNGA_ANALYSIS_ENGINE_COORDINATE_QUERY_H

// WP-5U1: immutable, Qt-free summary for a selected image coordinate. The
// query resolves image-global x/y to an Adam7 pass, pass-local row/x and stage
// byte/bit offsets without decoding or starting Deep Trace.

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <optional>
#include <string>

namespace pnga::analysis_engine {

enum class CoordinateQueryStatus {
  kReady = 0,
  kNoSelection = 1,
  kNotApplicable = 2,
  kOutOfRange = 3,
  kError = 4,
};

const char* coordinate_query_status_text(CoordinateQueryStatus status) noexcept;

struct CoordinateSummary {
  CoordinateQueryStatus status = CoordinateQueryStatus::kNoSelection;
  std::string error;

  // `selection` retains all dimensions supplied by the caller. Its image
  // coordinate is canonicalized to the derived pass and pass-local row.
  pnga::trace_model::Selection selection;
  std::optional<pnga::trace_model::ImageCoordinate> image;

  // Pass index is zero-based in the layout; pass_number follows the public
  // coordinate convention (0 for non-interlaced, 1..7 for Adam7).
  std::uint8_t pass_index = 0;
  std::uint8_t pass_number = 0;
  std::uint64_t stream_row = 0;
  std::uint64_t row_in_pass = 0;
  std::uint64_t local_x = 0;

  // Offsets are byte offsets into StageSet::filtered (excluding the filter
  // byte) and StageSet::unfiltered respectively. The bit fields identify the
  // selected native sample within those bytes.
  std::uint64_t filtered_data_offset = 0;
  std::uint64_t unfiltered_data_offset = 0;
  std::uint64_t sample_bit_offset = 0;  // filtered/pass-local byte
  std::uint64_t unfiltered_sample_bit_offset = 0;  // final image byte
  std::uint8_t sample_bit_length = 0;
  std::uint8_t sample_byte_count = 0;

  // Set only when a channel/sample dimension is selected. For a whole-pixel
  // selection, channel_count describes how many native samples are present.
  std::optional<std::uint64_t> native_sample_index;
  std::uint8_t channel_count = 0;
};

// Resolves a Selection against already materialized StageSet data. This is a
// bounded metadata query: it does not read a ByteSource, inflate, reverse
// filters or retain a token trace.
CoordinateSummary query_coordinate(const StageSet& stages,
                                   const pnga::trace_model::Selection& selection);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_COORDINATE_QUERY_H
