#ifndef PNGA_ANALYSIS_ENGINE_TRACE_QUERY_H
#define PNGA_ANALYSIS_ENGINE_TRACE_QUERY_H

// WP-5T0A: immutable, Qt-free contract for a bounded Deep Trace query.  The
// contract composes the already-indexed Deflate blocks and an on-demand token
// replay; it does not create threads, retain a whole-file trace, or perform
// GUI/decoder work.

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/provenance.h>
#include <pnga/trace-model/offset_range.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class TraceQueryStatus {
  kNotIndexed = 0,
  kReplaying = 1,
  kReady = 2,
  kPartial = 3,
  kError = 4,
  kCancelled = 5,
};

const char* trace_query_status_text(TraceQueryStatus status) noexcept;

struct TraceBlockSummary {
  std::uint64_t index = 0;
  pnga::deflate_index::BlockType type =
      pnga::deflate_index::BlockType::kStored;
  bool last = false;
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;
  std::vector<pnga::trace_model::ProvenanceSpan> physical_spans;

  // Block input positions include the zlib wrapper origin used by the fast
  // block index. Keeping this conversion named prevents callers from
  // confusing them with token-relative Deflate payload bits.
  pnga::trace_model::ZlibBitRange input_range() const noexcept {
    return {pnga::trace_model::ZlibBitOffset{input_bit_begin},
            pnga::trace_model::ZlibBitOffset{input_bit_end}};
  }
  pnga::trace_model::InflatedByteRange output_range() const noexcept {
    return {pnga::trace_model::InflatedByteOffset{output_begin},
            pnga::trace_model::InflatedByteOffset{output_end}};
  }

  bool operator==(const TraceBlockSummary&) const = default;
};

struct TraceTokenSummary {
  std::uint64_t index = 0;
  pnga::deflate_trace::TokenKind kind =
      pnga::deflate_trace::TokenKind::kLiteral;
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;
  std::uint8_t literal = 0;
  std::uint16_t length = 0;
  std::uint16_t distance = 0;
  std::uint64_t match_source_begin = 0;
  std::uint64_t match_source_end = 0;
  std::vector<pnga::deflate_trace::TokenOutputRange> match_source_ranges;
  std::int64_t block_index = -1;
  // WP-5U12D: the literal/length Huffman symbol consumed by this token,
  // captured by the decoder at the read that resolved it. Stored literals
  // and stored block boundaries consume no Huffman code and stay unset.
  std::optional<std::uint16_t> huffman_symbol;

  // Token input positions are relative to the DEFLATE payload; output
  // positions are absolute inflated-byte offsets.
  pnga::trace_model::DeflateBitRange input_range() const noexcept {
    return {pnga::trace_model::DeflateBitOffset{input_bit_begin},
            pnga::trace_model::DeflateBitOffset{input_bit_end}};
  }
  pnga::trace_model::InflatedByteRange output_range() const noexcept {
    return {pnga::trace_model::InflatedByteOffset{output_begin},
            pnga::trace_model::InflatedByteOffset{output_end}};
  }

  bool operator==(const TraceTokenSummary&) const = default;
};

struct TraceHuffmanTableSummary {
  pnga::deflate_trace::HuffmanTableKind kind =
      pnga::deflate_trace::HuffmanTableKind::kCodeLength;
  std::vector<pnga::deflate_trace::HuffmanTableEntry> entries;

  bool operator==(const TraceHuffmanTableSummary&) const = default;
};

// One bounded, immutable result. `blocks` and `tokens` contain only the
// ranges associated with the requested inflated output interval. A partial
// result retains all verified ranges and sets truncated when the token budget
// stopped the replay.
struct TraceQueryResult {
  TraceQueryStatus status = TraceQueryStatus::kNotIndexed;
  std::string error;
  std::uint64_t generation = 0;
  pnga::trace_model::Selection selection;

  std::uint64_t inflated_begin = 0;
  std::uint64_t inflated_end = 0;
  std::uint64_t output_bytes = 0;
  std::uint64_t deflate_data_begin = 0;
  bool truncated = false;

  std::vector<TraceBlockSummary> blocks;
  std::vector<TraceTokenSummary> tokens;
  std::vector<TraceHuffmanTableSummary> huffman_tables;
  std::vector<pnga::trace_model::ProvenanceSpan> logical_input;
  std::vector<pnga::trace_model::ProvenanceSpan> physical_input;
  std::vector<pnga::deflate_trace::TokenOutputRange> match_source_ranges;

  bool operator==(const TraceQueryResult&) const = default;
};

// Composes a bounded result from independently produced fast-index and trace
// artifacts. `inflated_end` is exclusive. No worker is started and no input
// payload is concatenated; physical spans are mapped through VirtualIDATStream.
TraceQueryResult compose_trace_query(
    std::uint64_t generation, const pnga::trace_model::Selection& selection,
    const pnga::deflate_index::BlockIndexResult& block_index,
    const pnga::deflate_trace::TokenDecodeResult& trace,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source, std::uint64_t inflated_begin,
    std::uint64_t inflated_end, std::uint64_t max_tokens);

// Stable, locale-independent text form for logs, cache keys and golden tests.
// The field order is fixed and every result field is represented.
std::string serialize_trace_query(const TraceQueryResult& result);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_TRACE_QUERY_H
