// WP-602A: immutable analysis-result adapter for statistics.

#include "pnga/analysis-engine/statistics_adapter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace pnga::analysis_engine {
namespace {

using pnga::statistics::BuildStatus;

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *output = left + right;
  return true;
}

bool checked_end(std::uint64_t begin, std::uint64_t length,
                 std::uint64_t* end) noexcept {
  return checked_add(begin, length, end);
}

pnga::statistics::StatisticsSnapshot invalid(const char* message) {
  pnga::statistics::StatisticsSnapshot result;
  result.status = BuildStatus::kInvalidInput;
  result.error = message;
  return result;
}

pnga::statistics::StatisticsSnapshot cancelled_result() {
  pnga::statistics::StatisticsSnapshot result;
  result.status = BuildStatus::kCancelled;
  result.error = "statistics adaptation cancelled";
  return result;
}

bool cancelled(const pnga::statistics::CancelPredicate& predicate,
               std::size_t index) {
  return static_cast<bool>(predicate) && index % 256 == 0 && predicate();
}

}  // namespace

pnga::statistics::StatisticsSnapshot collect_statistics(
    const StatisticsSources& sources, pnga::statistics::StatisticsLimits limits,
    pnga::statistics::CancelPredicate should_cancel) {
  if (limits.max_samples == 0) {
    return invalid("statistics sample budget must be positive");
  }

  std::vector<std::array<char, 4>> chunk_type_storage;
  std::vector<pnga::statistics::ChunkSample> chunk_samples;
  std::vector<pnga::statistics::FilterSample> filter_samples;
  std::vector<pnga::statistics::BlockSample> block_samples;
  std::vector<pnga::statistics::TokenSample> token_samples;

  std::uint64_t compressed_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  bool has_compression_totals = false;

  if (sources.chunks != nullptr) {
    if (sources.chunks->chunks.size() > limits.max_samples) {
      return invalid("statistics sample budget exceeded while adapting Chunks");
    }
    chunk_type_storage.reserve(sources.chunks->chunks.size());
    chunk_samples.reserve(sources.chunks->chunks.size());
    std::size_t index = 0;
    for (const auto& chunk : sources.chunks->chunks) {
      if (cancelled(should_cancel, index++)) {
        return cancelled_result();
      }
      auto& type = chunk_type_storage.emplace_back();
      for (std::size_t i = 0; i < type.size(); ++i) {
        type[i] = static_cast<char>(std::to_integer<unsigned char>(chunk.type[i]));
      }
      chunk_samples.push_back({std::string_view(type.data(), type.size()),
                               chunk.data_length});
      if (std::string_view(type.data(), type.size()) == "IDAT") {
        if (!checked_add(compressed_bytes, chunk.data_length,
                         &compressed_bytes)) {
          return invalid("compressed byte total overflow");
        }
        has_compression_totals = true;
      }
    }
  }

  if (sources.stages != nullptr) {
    if (!sources.stages->success) {
      return invalid("cannot collect statistics from failed stage analysis");
    }
    if (sources.stages->scanlines.size() > limits.max_samples) {
      return invalid("statistics sample budget exceeded while adapting filters");
    }
    filter_samples.reserve(sources.stages->scanlines.size());
    std::size_t index = 0;
    for (const auto& scanline : sources.stages->scanlines) {
      if (cancelled(should_cancel, index++)) {
        return cancelled_result();
      }
      std::uint64_t end = 0;
      if (scanline.length == 0 ||
          !checked_end(scanline.offset, scanline.length, &end) ||
          end > sources.stages->filtered.size()) {
        return invalid("filtered scanline range is outside its backing buffer");
      }
      const auto filter = std::to_integer<unsigned char>(
          sources.stages->filtered[static_cast<std::size_t>(scanline.offset)]);
      filter_samples.push_back({filter, scanline.length - 1});
    }
  }

  if (sources.blocks != nullptr) {
    if (!sources.blocks->success) {
      return invalid("cannot collect statistics from failed block index");
    }
    if (sources.blocks->blocks.size() > limits.max_samples) {
      return invalid("statistics sample budget exceeded while adapting blocks");
    }
    block_samples.reserve(sources.blocks->blocks.size());
    std::size_t index = 0;
    for (const auto& block : sources.blocks->blocks) {
      if (cancelled(should_cancel, index++)) {
        return cancelled_result();
      }
      if (block.input_bit_end < block.input_bit_begin ||
          block.output_end < block.output_begin) {
        return invalid("Deflate block range is inverted");
      }
      pnga::statistics::BlockKind kind;
      switch (block.type) {
        case pnga::deflate_index::BlockType::kStored:
          kind = pnga::statistics::BlockKind::kStored;
          break;
        case pnga::deflate_index::BlockType::kFixed:
          kind = pnga::statistics::BlockKind::kFixed;
          break;
        case pnga::deflate_index::BlockType::kDynamic:
          kind = pnga::statistics::BlockKind::kDynamic;
          break;
      }
      block_samples.push_back({kind, block.input_bit_end - block.input_bit_begin,
                               block.output_end - block.output_begin});
    }
    inflated_bytes = sources.blocks->total_output_bytes;
    has_compression_totals = has_compression_totals ||
                             sources.blocks->success;
  }

  if (sources.tokens != nullptr) {
    if (!sources.tokens->success) {
      return invalid("cannot collect statistics from failed token decode");
    }
    if (sources.tokens->tokens.size() > limits.max_samples) {
      return invalid("statistics sample budget exceeded while adapting tokens");
    }
    token_samples.reserve(sources.tokens->tokens.size());
    std::size_t index = 0;
    for (const auto& token : sources.tokens->tokens) {
      if (cancelled(should_cancel, index++)) {
        return cancelled_result();
      }
      if (token.input_bit_end < token.input_bit_begin ||
          token.output_end < token.output_begin) {
        return invalid("Deflate token range is inverted");
      }
      pnga::statistics::TokenKind kind;
      switch (token.kind) {
        case pnga::deflate_trace::TokenKind::kLiteral:
          kind = pnga::statistics::TokenKind::kLiteral;
          break;
        case pnga::deflate_trace::TokenKind::kLengthDistance:
          kind = pnga::statistics::TokenKind::kLengthDistance;
          break;
        case pnga::deflate_trace::TokenKind::kEndOfBlock:
          kind = pnga::statistics::TokenKind::kEndOfBlock;
          break;
      }
      token_samples.push_back({kind, token.input_bit_end - token.input_bit_begin,
                               token.output_end - token.output_begin,
                               token.length, token.distance});
    }
    inflated_bytes = sources.tokens->output_bytes;
    has_compression_totals = has_compression_totals || sources.tokens->success;
  }

  return pnga::statistics::collect(
      pnga::statistics::StatisticsInput{chunk_samples, filter_samples,
                                         block_samples, token_samples,
                                         compressed_bytes, inflated_bytes,
                                         has_compression_totals},
      limits, std::move(should_cancel));
}

}  // namespace pnga::analysis_engine
