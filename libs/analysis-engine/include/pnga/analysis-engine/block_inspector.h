#ifndef PNGA_ANALYSIS_ENGINE_BLOCK_INSPECTOR_H
#define PNGA_ANALYSIS_ENGINE_BLOCK_INSPECTOR_H

// WP-505A: a compact, Qt-free view model for the Deflate Block Inspector.
// This layer only projects the bounded TraceQueryResult; it never parses PNG
// or Deflate data and never owns a decoder or a worker.

#include "pnga/analysis-engine/trace_query.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class BlockInspectorStatus {
  kNoTrace = 0,
  kReady = 1,
  kPartial = 2,
  kError = 3,
};

const char* block_inspector_status_text(BlockInspectorStatus status) noexcept;

struct BlockInspectorRow {
  std::uint64_t block_index = 0;
  pnga::deflate_index::BlockType type =
      pnga::deflate_index::BlockType::kStored;
  bool last = false;
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;
  std::vector<pnga::trace_model::ProvenanceSpan> physical_spans;
  std::optional<std::uint64_t> current_output_position;

  bool operator==(const BlockInspectorRow&) const = default;
};

// The scanline is supplied by the caller that owns PNG geometry. TraceQuery
// deliberately remains backend-neutral, so Adam7/pass mapping is not
// reimplemented here. `current_output_position` is an absolute inflated byte
// offset, when the selected output byte belongs to a returned block.
struct BlockInspectorView {
  BlockInspectorStatus status = BlockInspectorStatus::kNoTrace;
  std::string error;
  std::uint64_t generation = 0;
  std::optional<std::uint64_t> scanline;
  std::optional<std::uint64_t> selected_output_offset;
  std::optional<std::uint64_t> selected_block_index;
  std::vector<BlockInspectorRow> rows;

  bool operator==(const BlockInspectorView&) const = default;
};

BlockInspectorView build_block_inspector(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_output_offset = std::nullopt,
    std::optional<std::uint64_t> scanline = std::nullopt);

// Stable, locale-independent form for diagnostics and golden tests.
std::string serialize_block_inspector(const BlockInspectorView& view);

// Generation-scoped projection of the complete Fast Block Index. Unlike the
// bounded TraceQueryResult, this view never contains token/event vectors and
// remains available while a Deep Trace window is replaying.
enum class FastCompressionIndexStatus {
  kUnavailable = 0,
  kReady = 1,
  kPartial = 2,
  kError = 3,
};

const char* fast_compression_index_status_text(
    FastCompressionIndexStatus status) noexcept;

struct FastCompressionStreamSummary {
  pnga::trace_model::ZlibByteRange stream_range{};
  pnga::trace_model::ZlibBitOffset deflate_data_begin{};
  std::uint64_t idat_segment_count = 0;
  std::uint64_t total_output_bytes = 0;
  bool adler_ok = false;

  bool operator==(const FastCompressionStreamSummary&) const = default;
};

struct FastCompressionBlockRow {
  std::uint64_t block_index = 0;
  pnga::deflate_index::BlockType type =
      pnga::deflate_index::BlockType::kStored;
  bool last = false;
  pnga::trace_model::ZlibBitRange input_range{};
  pnga::trace_model::InflatedByteRange output_range{};
  std::vector<pnga::trace_model::ProvenanceSpan> physical_spans;

  bool operator==(const FastCompressionBlockRow&) const = default;
};

struct FastCompressionIndexView {
  FastCompressionIndexStatus status = FastCompressionIndexStatus::kUnavailable;
  std::string error;
  std::uint64_t generation = 0;
  FastCompressionStreamSummary stream;
  std::vector<FastCompressionBlockRow> blocks;

  bool operator==(const FastCompressionIndexView&) const = default;
};

FastCompressionIndexView build_fast_compression_index(
    std::uint64_t generation,
    const pnga::deflate_index::BlockIndexResult& block_index,
    const pnga::png_format::VirtualIDATStream& stream);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_BLOCK_INSPECTOR_H
