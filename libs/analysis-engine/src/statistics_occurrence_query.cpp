// WP-602F: bounded occurrence resolution. Direct domains (chunk, filter,
// block) read the existing indexes; token domains stream the virtual IDAT
// stream through the scalar token scan with an O(1)-retention observer (no
// occurrence list is ever stored). Physical provenance maps the matched
// range through VirtualIDATStream::logical_to_physical so every cross-IDAT
// span is retained in order. All offset arithmetic is checked; untrusted
// keys and inconsistent indexes fail with stable errors.

#include "pnga/analysis-engine/statistics_occurrence_query.h"

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <limits>
#include <optional>
#include <vector>

namespace pnga::analysis_engine {
namespace {

using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::BlockType;
using pnga::png_format::ChunkNode;
using pnga::png_format::PhysicalRange;
using pnga::png_format::VirtualIDATStream;
using pnga::trace_model::BitSpan;
using pnga::trace_model::ImageCoordinate;
using pnga::trace_model::Selection;
using pnga::trace_model::Stage;
using pnga::trace_model::StreamSpan;

// RFC 1950: a non-FDICT zlib stream's Deflate data starts after the two-byte
// header. The scalar scan rejects FDICT, so the wrapper is always 16 bits.
constexpr std::uint64_t kZlibWrapperBits = 16;
constexpr std::uint64_t kChunkHeaderBytes = 8;
constexpr std::uint64_t kChunkCrcBytes = 4;

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *output = left + right;
  return true;
}

// Adapts the virtual IDAT stream to IByteSource without concatenating the
// payloads. Borrowed: the stream and the file source must outlive the
// adapter, guaranteed by the query scope.
class VirtualIdatByteSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatByteSource(const VirtualIDATStream& stream,
                        const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

// Maps an IDAT-logical bit range to the ordered physical file byte spans and
// the logical byte envelope. A zero-bit boundary maps to no span.
bool map_logical_bits(const VirtualIDATStream& stream,
                      std::uint64_t logical_bit_begin,
                      std::uint64_t logical_bit_end, Selection* selection,
                      std::string* error) {
  if (logical_bit_end < logical_bit_begin) {
    *error = "occurrence bit range is inverted";
    return false;
  }
  const std::uint64_t byte_begin = logical_bit_begin / 8;
  std::uint64_t rounded_end = 0;
  if (!checked_add(logical_bit_end, 7, &rounded_end)) {
    *error = "occurrence bit range overflow";
    return false;
  }
  const std::uint64_t byte_end = rounded_end / 8;
  if (byte_end == byte_begin) {
    return true;  // a zero-bit boundary consumes no data bytes
  }
  const std::uint64_t length = byte_end - byte_begin;
  std::vector<PhysicalRange> ranges;
  if (!stream.logical_to_physical(byte_begin, length, ranges)) {
    *error = "occurrence is outside the virtual IDAT stream";
    return false;
  }
  std::uint64_t covered = 0;
  for (const PhysicalRange& range : ranges) {
    if (!checked_add(covered, range.length, &covered)) {
      *error = "occurrence physical span overflow";
      return false;
    }
    selection->physical_spans.push_back(
        BitSpan{range.offset, range.length, 0, false});
  }
  if (covered != length) {
    *error = "occurrence physical spans do not tile the input envelope";
    return false;
  }
  selection->logical = StreamSpan{byte_begin, length};
  return true;
}

StatisticsOccurrenceResult make_error(std::uint64_t generation,
                                      std::string message) {
  StatisticsOccurrenceResult result;
  result.status = OccurrenceStatus::kError;
  result.error = std::move(message);
  result.generation = generation;
  return result;
}

// --- direct domains -----------------------------------------------------------

// One occurrence of a bucket inside an ordered index: the position used as
// the next/previous cursor and the payload index.
struct DirectOccurrence {
  std::uint64_t cursor = 0;
  std::size_t index = 0;
};

// Resolves first/previous/next over an ordered occurrence list using the
// caller-supplied cursor. Returns false when no occurrence matches.
bool resolve_direct(OccurrenceDirection direction,
                    const std::optional<std::uint64_t>& after_output_offset,
                    const std::vector<DirectOccurrence>& occurrences,
                    DirectOccurrence* resolved) {
  if (occurrences.empty()) {
    return false;
  }
  switch (direction) {
    case OccurrenceDirection::kFirst:
      *resolved = occurrences.front();
      return true;
    case OccurrenceDirection::kNext: {
      if (!after_output_offset.has_value()) {
        return false;
      }
      for (const DirectOccurrence& occurrence : occurrences) {
        if (occurrence.cursor > *after_output_offset) {
          *resolved = occurrence;
          return true;
        }
      }
      return false;
    }
    case OccurrenceDirection::kPrevious: {
      if (!after_output_offset.has_value()) {
        return false;
      }
      for (auto it = occurrences.rbegin(); it != occurrences.rend(); ++it) {
        if (it->cursor < *after_output_offset) {
          *resolved = *it;
          return true;
        }
      }
      return false;
    }
  }
  return false;
}

StatisticsOccurrenceResult run_chunk_occurrence(
    std::uint64_t generation, const pnga::png_format::ChunkIndex& chunks,
    const StatisticsNavigationRequest& request) {
  if (request.key.size() != 4) {
    return make_error(generation,
                      "chunk type key must be exactly four characters");
  }
  std::vector<DirectOccurrence> occurrences;
  for (std::size_t i = 0; i < chunks.chunks.size(); ++i) {
    if (chunks.chunks[i].text() == request.key) {
      occurrences.push_back(
          DirectOccurrence{chunks.chunks[i].header_offset, i});
    }
  }
  StatisticsOccurrenceResult result;
  result.generation = generation;
  DirectOccurrence resolved;
  if (!resolve_direct(request.direction, request.after_output_offset,
                      occurrences, &resolved)) {
    result.status = OccurrenceStatus::kNotFound;
    return result;
  }
  const ChunkNode& node = chunks.chunks[resolved.index];
  Selection& selection = result.selection;
  selection.node = static_cast<pnga::trace_model::NodeId>(resolved.index);
  selection.stage = Stage::kChunk;
  selection.physical_spans = {
      BitSpan{node.header_offset, kChunkHeaderBytes, 0, false},
      BitSpan{node.data_offset, node.data_length, 0, false},
      BitSpan{node.crc_offset, kChunkCrcBytes, 0, false}};
  result.status = OccurrenceStatus::kReady;
  return result;
}

StatisticsOccurrenceResult run_block_occurrence(
    std::uint64_t generation, const BlockIndexResult& blocks,
    const StatisticsNavigationRequest& request,
    const VirtualIDATStream& stream) {
  BlockType wanted = BlockType::kStored;
  if (request.key == "stored") {
    wanted = BlockType::kStored;
  } else if (request.key == "fixed") {
    wanted = BlockType::kFixed;
  } else if (request.key == "dynamic") {
    wanted = BlockType::kDynamic;
  } else {
    return make_error(generation,
                      "block type key must be stored, fixed or dynamic");
  }
  std::vector<DirectOccurrence> occurrences;
  for (std::size_t i = 0; i < blocks.blocks.size(); ++i) {
    if (blocks.blocks[i].type == wanted) {
      occurrences.push_back(
          DirectOccurrence{blocks.blocks[i].output_begin, i});
    }
  }
  StatisticsOccurrenceResult result;
  result.generation = generation;
  DirectOccurrence resolved;
  if (!resolve_direct(request.direction, request.after_output_offset,
                      occurrences, &resolved)) {
    // A verified prefix that holds no occurrence is not evidence of
    // absence: past the prefix the answer is unknown, not not_found.
    if (!blocks.success) {
      result.status = OccurrenceStatus::kPartial;
      result.error = "block navigation reached the verified block index "
                     "prefix";
      return result;
    }
    result.status = OccurrenceStatus::kNotFound;
    return result;
  }
  const pnga::deflate_index::DeflateBlock& block =
      blocks.blocks[resolved.index];
  // A previous answer over a partial prefix is only the true nearest
  // occurrence when the cursor lies inside the verified prefix.
  if (!blocks.success && request.direction == OccurrenceDirection::kPrevious &&
      request.after_output_offset.has_value() &&
      *request.after_output_offset > block.output_end) {
    result.status = OccurrenceStatus::kPartial;
    result.error = "block navigation reached the verified block index prefix";
    return result;
  }
  Selection& selection = result.selection;
  selection.stage = Stage::kTrace;
  std::string error;
  if (!map_logical_bits(stream, block.input_bit_begin, block.input_bit_end,
                        &selection, &error)) {
    return make_error(generation, error);
  }
  result.status = OccurrenceStatus::kReady;
  return result;
}

StatisticsOccurrenceResult run_filter_occurrence(
    std::uint64_t generation, const StageSet& stages,
    const StatisticsNavigationRequest& request,
    const VirtualIDATStream& stream, const BlockIndexResult& blocks) {
  if (request.key.size() != 1 || request.key[0] < '0' ||
      request.key[0] > '4') {
    return make_error(generation, "filter type key must be 0 to 4");
  }
  if (blocks.blocks.empty()) {
    return make_error(generation,
                      "filter navigation requires a usable block index");
  }
  const unsigned wanted = static_cast<unsigned>(request.key[0] - '0');
  std::vector<DirectOccurrence> occurrences;
  for (std::size_t i = 0; i < stages.scanlines.size(); ++i) {
    const auto& span = stages.scanlines[i];
    if (span.length == 0 || span.offset >= stages.filtered.size()) {
      continue;  // an unusable span carries no filter byte
    }
    const unsigned filter = std::to_integer<unsigned>(
        stages.filtered[static_cast<std::size_t>(span.offset)]);
    if (filter == wanted) {
      occurrences.push_back(DirectOccurrence{span.offset, i});
    }
  }
  StatisticsOccurrenceResult result;
  result.generation = generation;
  DirectOccurrence resolved;
  if (!resolve_direct(request.direction, request.after_output_offset,
                      occurrences, &resolved)) {
    result.status = OccurrenceStatus::kNotFound;
    return result;
  }
  const auto& scanline = stages.scanlines[resolved.index];
  // The compressed provenance of the scanline is the byte envelope of the
  // indexed block that contains its first inflated byte.
  const std::optional<std::size_t> block_index =
      pnga::deflate_index::block_for_output(blocks, scanline.offset);
  if (!block_index.has_value()) {
    if (!blocks.success) {
      // The scanline's block lies beyond the verified index prefix: the
      // provenance is unknown, not inconsistent.
      StatisticsOccurrenceResult partial;
      partial.generation = generation;
      partial.status = OccurrenceStatus::kPartial;
      partial.error =
          "filter navigation reached the verified block index prefix";
      return partial;
    }
    return make_error(generation,
                      "scanline is outside the indexed inflated output");
  }
  Selection& selection = result.selection;
  selection.stage = Stage::kFiltered;
  ImageCoordinate image;
  image.row = resolved.index;  // stream-order row hint (pass-major)
  selection.image = image;
  std::string error;
  if (!map_logical_bits(stream, blocks.blocks[*block_index].input_bit_begin,
                        blocks.blocks[*block_index].input_bit_end, &selection,
                        &error)) {
    return make_error(generation, error);
  }
  result.status = OccurrenceStatus::kReady;
  return result;
}

// --- token domains ------------------------------------------------------------

// Absolute Deflate bit anchoring. A TokenFact carries only its per-token
// input width, so the query anchors each block: stored and fixed blocks have
// computable first-token positions (stored: byte-aligned header + LEN/NLEN;
// fixed: three header bits); a dynamic block is anchored when its
// end-of-block fact arrives (the EOB is the block's last token, so the
// header width is the block width minus the summed token widths). first/
// next delay their stop until the containing block's anchor exists so the
// match maps to exact input bits; the scan stays bounded by the request
// budgets, and an unanchored match falls back to the containing block's
// byte envelope.
struct TokenMatch {
  bool found = false;
  bool exact = false;
  std::size_t block_ordinal = 0;
  std::uint64_t width_before_in_block = 0;
  std::uint64_t match_width = 0;
  std::uint64_t deflate_begin = 0;
  std::uint64_t deflate_end = 0;
};

std::uint64_t align_to_byte(std::uint64_t bit_position, bool* ok) noexcept {
  std::uint64_t rounded = 0;
  *ok = checked_add(bit_position, 7, &rounded);
  return rounded & ~std::uint64_t{7};
}

StatisticsOccurrenceResult run_token_occurrence(
    std::uint64_t generation, const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& chunks,
    const BlockIndexResult& blocks, const StatisticsNavigationRequest& request,
    const CancellationToken* cancellation) {
  pnga::deflate_trace::TokenKind wanted =
      pnga::deflate_trace::TokenKind::kLiteral;
  std::uint64_t wanted_value = 0;
  bool have_wanted_value = false;
  switch (request.domain) {
    case StatisticsBucketDomain::kTokenKind:
      if (request.key == "literal") {
        wanted = pnga::deflate_trace::TokenKind::kLiteral;
      } else if (request.key == "match") {
        wanted = pnga::deflate_trace::TokenKind::kLengthDistance;
      } else if (request.key == "eob") {
        wanted = pnga::deflate_trace::TokenKind::kEndOfBlock;
      } else {
        return make_error(
            generation, "token kind key must be literal, match or eob");
      }
      break;
    case StatisticsBucketDomain::kLength:
    case StatisticsBucketDomain::kDistance: {
      std::uint64_t value = 0;
      for (const char c : request.key) {
        if (c < '0' || c > '9' ||
            value > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) /
                        10) {
          return make_error(generation,
                            request.domain == StatisticsBucketDomain::kLength
                                ? "invalid length key"
                                : "invalid distance key");
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
      }
      wanted = pnga::deflate_trace::TokenKind::kLengthDistance;
      wanted_value = value;
      have_wanted_value = true;
      break;
    }
    default:
      return make_error(generation, "domain is not a token domain");
  }

  if (blocks.blocks.empty()) {
    return make_error(generation,
                      "token navigation requires a usable block index");
  }

  StatisticsOccurrenceResult result;
  result.generation = generation;

  VirtualIDATStream stream(chunks);
  VirtualIdatByteSource logical(stream, source);

  // The block index carries the zlib wrapper origin (16 bits for PNG's
  // non-FDICT streams, the value the scalar scan enforces). Token fact
  // widths are DEFLATE-relative and are translated with it; the constant
  // below is only the fallback for an index without a wrapper origin.
  const std::uint64_t wrapper_bits = blocks.zlib_header_bits != 0
                                         ? blocks.zlib_header_bits
                                         : kZlibWrapperBits;

  // Observer state: the block cursor advances with the absolute output
  // offsets (blocks tile the inflated output), the width counter sums token
  // widths inside the current block, and the anchor holds the current
  // block's first-token position when it is known. Anchors live in
  // DEFLATE-relative bits (token fact widths are DEFLATE-relative); the
  // block index positions include the zlib wrapper origin.
  TokenMatch best;
  std::size_t block_ordinal = 0;
  std::uint64_t width_in_block = 0;
  bool anchor_known = false;
  std::uint64_t anchor_bit = 0;
  bool overflow = false;
  // Set when the scan consumes the last indexed block's end-of-block while
  // the index is a verified prefix: every later fact belongs to a block the
  // index never verified and cannot be anchored, so the scan stops instead
  // of misattributing it.
  bool prefix_exhausted = false;

  const auto compute_anchor = [&](const pnga::deflate_index::DeflateBlock&
                                      block) {
    anchor_known = false;
    if (block.input_bit_begin < wrapper_bits) {
      return;
    }
    const std::uint64_t begin = block.input_bit_begin - wrapper_bits;
    switch (block.type) {
      case BlockType::kFixed:
        anchor_bit = begin + 3;
        anchor_known = true;
        break;
      case BlockType::kStored: {
        bool ok = true;
        const std::uint64_t header_end = align_to_byte(begin + 3, &ok);
        anchor_bit = header_end + 32;  // LEN + NLEN
        anchor_known = ok;
        break;
      }
      case BlockType::kDynamic:
        anchor_known = false;  // recovered when the block's EOB arrives
        break;
    }
  };
  if (!blocks.blocks.empty()) {
    compute_anchor(blocks.blocks.front());
  }

  const auto retro_fix = [&](const pnga::deflate_index::DeflateBlock& block,
                             std::uint64_t total_token_width) {
    // header width = block width - summed token widths; the anchor is the
    // DEFLATE-relative block start plus the header width.
    if (block.input_bit_begin < wrapper_bits) {
      return;
    }
    const std::uint64_t begin = block.input_bit_begin - wrapper_bits;
    const std::uint64_t block_width =
        block.input_bit_end - block.input_bit_begin;
    if (block_width < total_token_width) {
      return;
    }
    anchor_bit = begin + (block_width - total_token_width);
    anchor_known = true;
    if (best.found && !best.exact &&
        best.block_ordinal == block_ordinal) {
      std::uint64_t match_begin = 0;
      std::uint64_t match_end = 0;
      if (!checked_add(anchor_bit, best.width_before_in_block,
                       &match_begin) ||
          !checked_add(match_begin, best.match_width, &match_end)) {
        overflow = true;
        return;
      }
      best.deflate_begin = match_begin;
      best.deflate_end = match_end;
      best.exact = true;
    }
  };

  const auto cancelled = [&] {
    return cancellation != nullptr && cancellation->cancelled();
  };

  pnga::deflate_trace::TokenScanOptions options;
  options.max_tokens = request.max_tokens;
  options.max_input_bytes = request.max_input_bytes;
  options.should_cancel = cancelled;
  options.observer = [&](const pnga::deflate_trace::TokenFact& fact) {
    if (prefix_exhausted) {
      return false;  // beyond the verified prefix nothing can be anchored
    }
    std::uint64_t deflate_begin = 0;
    std::uint64_t deflate_end = 0;
    bool exact = false;
    if (fact.kind != pnga::deflate_trace::TokenKind::kEndOfBlock) {
      // Advance the block cursor; blocks tile the inflated output.
      while (block_ordinal + 1 < blocks.blocks.size() &&
             fact.output_begin >=
                 blocks.blocks[block_ordinal].output_end) {
        ++block_ordinal;
        width_in_block = 0;
        compute_anchor(blocks.blocks[block_ordinal]);
      }
      if (anchor_known) {
        std::uint64_t begin = 0;
        if (!checked_add(anchor_bit, width_in_block, &begin) ||
            !checked_add(begin, fact.input_bits, &deflate_end)) {
          overflow = true;
          return false;
        }
        deflate_begin = begin;
        exact = true;
      }
    }

    bool matches = fact.kind == wanted;
    if (matches && have_wanted_value) {
      matches = request.domain == StatisticsBucketDomain::kLength
                    ? fact.length == wanted_value
                    : fact.distance == wanted_value;
    }
    if (matches) {
      bool record = false;
      switch (request.direction) {
        case OccurrenceDirection::kFirst:
          record = !best.found;  // the first match wins
          break;
        case OccurrenceDirection::kNext:
          record = !best.found && request.after_output_offset.has_value() &&
                   fact.output_begin > *request.after_output_offset;
          break;
        case OccurrenceDirection::kPrevious:
          record = request.after_output_offset.has_value() &&
                   fact.output_begin < *request.after_output_offset;
          break;
      }
      if (record) {
        best.found = true;
        best.exact = exact;
        best.block_ordinal = block_ordinal;
        best.width_before_in_block = width_in_block;
        best.deflate_begin = deflate_begin;
        best.deflate_end = deflate_end;
        best.match_width = fact.input_bits;
      }
    }

    // Width accounting and block-cursor advancement. The EOB belongs to
    // the block being decoded; the NEXT fact belongs to the next block, so
    // the cursor advances here — empty blocks emit consecutive EOB facts
    // at the same output offset and would otherwise be misattributed to
    // the previous block.
    std::uint64_t total_width = 0;
    if (!checked_add(width_in_block, fact.input_bits, &total_width)) {
      overflow = true;
      return false;
    }
    if (fact.kind == pnga::deflate_trace::TokenKind::kEndOfBlock) {
      // The EOB is the current block's last token: retro_fix recovers this
      // block's anchor from the summed token widths (which pins any EOB
      // match to end - width) and the cursor then advances so the NEXT
      // fact belongs to the next block. Empty blocks emit consecutive EOB
      // facts at the same output offset and would otherwise be
      // misattributed to the previous block.
      const pnga::deflate_index::DeflateBlock& block =
          blocks.blocks[block_ordinal];
      retro_fix(block, total_width);
      if (block_ordinal + 1 < blocks.blocks.size()) {
        ++block_ordinal;
        width_in_block = 0;
        compute_anchor(blocks.blocks[block_ordinal]);
      } else {
        width_in_block = total_width;
        if (!blocks.success) {
          // The index is a verified prefix: the next fact belongs to a
          // block the index never verified. Stop the scan instead of
          // misattributing it.
          prefix_exhausted = true;
          return false;
        }
      }
    } else {
      width_in_block = total_width;
    }

    // first/next stop as soon as the recorded match carries exact input
    // bits (immediately for anchored blocks, at the block's EOB otherwise);
    // previous always keeps scanning so the last match before the cursor
    // wins.
    if (request.direction == OccurrenceDirection::kPrevious) {
      return true;
    }
    return !(best.found && best.exact);
  };

  const pnga::deflate_trace::TokenScanResult scan =
      pnga::deflate_trace::scan_tokens(logical, options);
  result.searched_tokens = scan.token_count;
  std::uint64_t rounded_input = 0;
  if (!checked_add(scan.input_bits, 7, &rounded_input)) {
    return make_error(generation, "occurrence token accounting overflow");
  }
  result.searched_input_bytes = rounded_input / 8;

  if (scan.status == pnga::deflate_trace::TokenScanStatus::kCancelled ||
      cancelled()) {
    result.status = OccurrenceStatus::kCancelled;
    result.error = "occurrence scan cancelled";
    return result;
  }
  if (overflow) {
    return make_error(generation, "occurrence token accounting overflow");
  }
  // Honesty over a verified-prefix index: the searched prefix's end is the
  // last verified block's output end.
  const bool index_complete = blocks.success;
  const std::uint64_t prefix_end_output = blocks.blocks.back().output_end;
  if (!index_complete && !best.found) {
    // The verified prefix held no occurrence. A previous query whose cursor
    // the scan already covered is conclusive; for every other direction the
    // unindexed remainder may still hold occurrences, so the answer is
    // unknown — never a whole-stream not_found.
    const bool covered_cursor =
        request.direction == OccurrenceDirection::kPrevious &&
        request.after_output_offset.has_value() &&
        *request.after_output_offset <= scan.output_bytes;
    if (!covered_cursor) {
      result.status = OccurrenceStatus::kPartial;
      result.error =
          "token navigation reached the verified block index prefix";
      return result;
    }
    result.status = OccurrenceStatus::kNotFound;
    return result;
  }
  if (best.found && request.direction == OccurrenceDirection::kPrevious &&
      request.after_output_offset.has_value() &&
      scan.status == pnga::deflate_trace::TokenScanStatus::kBudgetExceeded &&
      scan.output_bytes < *request.after_output_offset) {
    // The budget stopped the scan before it reached the cursor: the last
    // match inside the searched prefix is not necessarily the nearest one
    // before the cursor, so the honest answer is the searched-range partial.
    result.status = OccurrenceStatus::kPartial;
    result.error = "occurrence scan reached the occurrence budget before "
                   "the cursor";
    return result;
  }
  if (best.found && !index_complete &&
      request.direction == OccurrenceDirection::kPrevious &&
      request.after_output_offset.has_value() &&
      *request.after_output_offset > prefix_end_output) {
    // The cursor lies beyond the verified prefix: the nearest previous
    // occurrence may live in the unindexed region, so even a found match
    // is not provably the nearest one.
    result.status = OccurrenceStatus::kPartial;
    result.error =
        "token navigation reached the verified block index prefix";
    return result;
  }
  if (best.found) {
    // A recorded match is verified-prefix evidence; build the typed
    // selection with every cross-IDAT physical span.
    Selection& selection = result.selection;
    selection.stage = Stage::kTrace;
    std::uint64_t logical_begin = 0;
    std::uint64_t logical_end = 0;
    if (best.exact && best.deflate_begin == best.deflate_end) {
      // Zero-width occurrence (a stored block's boundary EOB): honest
      // kReady at the exact boundary byte, consuming no input bytes.
      if (!checked_add(wrapper_bits, best.deflate_begin, &logical_begin)) {
        return make_error(generation, "occurrence token accounting overflow");
      }
      selection.physical_spans.push_back(
          BitSpan{logical_begin / 8, 0, 0, false});
      selection.logical = StreamSpan{logical_begin / 8, 0};
      result.status = OccurrenceStatus::kReady;
      return result;
    }
    std::string error;
    if (best.exact) {
      if (!checked_add(wrapper_bits, best.deflate_begin, &logical_begin) ||
          !checked_add(wrapper_bits, best.deflate_end, &logical_end)) {
        return make_error(generation, "occurrence token accounting overflow");
      }
      if (!map_logical_bits(stream, logical_begin, logical_end, &selection,
                            &error)) {
        return make_error(generation, error);
      }
    } else {
      // Unanchored fallback: the containing block's byte envelope.
      const pnga::deflate_index::DeflateBlock& block =
          blocks.blocks[best.block_ordinal];
      if (!map_logical_bits(stream, block.input_bit_begin,
                            block.input_bit_end, &selection, &error)) {
        return make_error(generation, error);
      }
    }
    result.status = OccurrenceStatus::kReady;
    return result;
  }
  switch (scan.status) {
    case pnga::deflate_trace::TokenScanStatus::kReady:
    case pnga::deflate_trace::TokenScanStatus::kPartial:
      result.status = OccurrenceStatus::kNotFound;
      return result;
    case pnga::deflate_trace::TokenScanStatus::kBudgetExceeded:
      result.status = OccurrenceStatus::kPartial;
      result.error = "occurrence scan reached the occurrence budget";
      return result;
    case pnga::deflate_trace::TokenScanStatus::kInvalidInput:
    case pnga::deflate_trace::TokenScanStatus::kError:
      result.status = OccurrenceStatus::kError;
      result.error = scan.error.empty() ? "failed to read the token stream"
                                        : scan.error;
      return result;
    case pnga::deflate_trace::TokenScanStatus::kCancelled:
      result.status = OccurrenceStatus::kCancelled;
      result.error = "occurrence scan cancelled";
      return result;
  }
  result.status = OccurrenceStatus::kError;
  result.error = "failed to read the token stream";
  return result;
}

}  // namespace

StatisticsOccurrenceResult query_statistics_occurrence(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& chunks, const StageSet* stages,
    const BlockIndexResult* blocks, const StatisticsNavigationRequest& request,
    const CancellationToken* cancellation) {
  const std::uint64_t generation = request.generation;
  if (cancellation != nullptr && cancellation->cancelled()) {
    StatisticsOccurrenceResult result;
    result.status = OccurrenceStatus::kCancelled;
    result.error = "occurrence query cancelled";
    result.generation = generation;
    return result;
  }

  switch (request.domain) {
    case StatisticsBucketDomain::kChunkType:
      return run_chunk_occurrence(generation, chunks, request);
    case StatisticsBucketDomain::kBlockType: {
      if (blocks == nullptr) {
        return make_error(generation,
                          "block navigation requires the block index");
      }
      VirtualIDATStream stream(chunks);
      return run_block_occurrence(generation, *blocks, request, stream);
    }
    case StatisticsBucketDomain::kFilterType: {
      if (stages == nullptr) {
        return make_error(generation,
                          "filter navigation requires the stage analysis");
      }
      if (!stages->success) {
        return make_error(
            generation, "filter navigation requires a successful stage "
                        "analysis");
      }
      if (blocks == nullptr) {
        return make_error(generation,
                          "filter navigation requires the block index");
      }
      VirtualIDATStream stream(chunks);
      return run_filter_occurrence(generation, *stages, request, stream,
                                   *blocks);
    }
    case StatisticsBucketDomain::kTokenKind:
    case StatisticsBucketDomain::kLength:
    case StatisticsBucketDomain::kDistance: {
      if (blocks == nullptr) {
        return make_error(generation,
                          "token navigation requires the block index");
      }
      return run_token_occurrence(generation, source, chunks, *blocks,
                                  request, cancellation);
    }
  }
  return make_error(generation, "unknown statistics bucket domain");
}

}  // namespace pnga::analysis_engine
