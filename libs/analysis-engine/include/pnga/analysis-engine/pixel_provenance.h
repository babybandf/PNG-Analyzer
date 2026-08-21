#ifndef PNGA_ANALYSIS_ENGINE_PIXEL_PROVENANCE_H
#define PNGA_ANALYSIS_ENGINE_PIXEL_PROVENANCE_H

// WP-504: on-demand pixel/channel provenance. The query composes the
// reconstruction stages with a bounded Deep Trace replay; it never assumes a
// one-pixel/one-token relationship.

#include "pnga/analysis-engine/stage_analysis.h"

#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/provenance.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

struct PixelProvenanceResult {
  bool success = false;
  std::string error;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t channel = 0;

  std::vector<pnga::trace_model::ProvenanceSpan> native_samples;
  std::vector<pnga::trace_model::ProvenanceSpan> reconstructed;
  std::vector<pnga::trace_model::ProvenanceSpan> filtered;
  std::vector<pnga::trace_model::ProvenanceSpan> inflated;
  std::vector<pnga::deflate_trace::TokenOutputRange> token_output_ranges;
  std::vector<pnga::trace_model::ProvenanceSpan> logical_input;
  std::vector<pnga::trace_model::ProvenanceSpan> physical_input;
  std::vector<pnga::deflate_trace::TokenOutputRange> match_source_ranges;
};

// Maps one native pixel channel through the materialized reconstruction stages
// and an on-demand token trace. `max_trace_output` is an explicit resource
// cap; callers should set it to the expected inflated stream size or a larger
// approved budget. The IDAT stream is consumed through its logical adapter,
// so input spans remain valid across arbitrary IDAT boundaries.
PixelProvenanceResult query_pixel_provenance(
    const StageSet& stages,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source, std::uint64_t x, std::uint64_t y,
    std::uint64_t channel, std::uint64_t max_trace_output);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_PIXEL_PROVENANCE_H
