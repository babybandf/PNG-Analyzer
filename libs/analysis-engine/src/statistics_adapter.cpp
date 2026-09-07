// WP-602A/602B: immutable analysis-result adapter for per-section statistics.

#include "pnga/analysis-engine/statistics_adapter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace pnga::analysis_engine {
namespace {

using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsSectionId;

constexpr std::size_t kCheckInterval = 256;

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *output = left + right;
  return true;
}

bool cancelled(const pnga::statistics::CancelPredicate& predicate,
               std::size_t index) {
  return static_cast<bool>(predicate) && index % kCheckInterval == 0 &&
         predicate();
}

}  // namespace

pnga::statistics::StatisticsSnapshot collect_statistics(
    const StatisticsSources& sources, pnga::statistics::StatisticsLimits limits,
    pnga::statistics::CancelPredicate should_cancel) {
  if (limits.max_samples == 0) {
    pnga::statistics::StatisticsAccumulator accumulator(limits);
    for (const StatisticsSectionId id :
         {StatisticsSectionId::kOverview, StatisticsSectionId::kChunks,
          StatisticsSectionId::kFilters, StatisticsSectionId::kBlocks,
          StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
          StatisticsSectionId::kDistances}) {
      accumulator.finish(id, SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget must be positive");
    }
    return accumulator.snapshot();
  }

  pnga::statistics::StatisticsAccumulator accumulator(limits);
  bool cancelled_stop = false;
  std::uint64_t compressed_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  bool has_compressed = false;  // totals from a fully adapted Chunks source
  bool has_inflated = false;    // totals from fully adapted Blocks or Tokens

  if (sources.chunks != nullptr && !cancelled_stop) {
    const auto& chunk_index = *sources.chunks;
    if (chunk_index.chunks.size() > limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kChunks,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget exceeded while adapting Chunks");
    } else {
      std::uint64_t idat_bytes = 0;
      bool any_idat = false;
      std::size_t index = 0;
      bool stopped = false;
      for (const auto& chunk : chunk_index.chunks) {
        if (cancelled(should_cancel, index++)) {
          accumulator.finish(StatisticsSectionId::kChunks,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             "statistics adaptation cancelled");
          cancelled_stop = true;
          stopped = true;
          break;
        }
        std::array<char, 4> type{};
        for (std::size_t i = 0; i < type.size(); ++i) {
          type[i] =
              static_cast<char>(std::to_integer<unsigned char>(chunk.type[i]));
        }
        const std::string_view type_view(type.data(), type.size());
        if (!accumulator.add(
                pnga::statistics::ChunkSample{type_view, chunk.data_length})) {
          stopped = true;
          break;
        }
        if (type_view == "IDAT" &&
            !checked_add(idat_bytes, chunk.data_length, &idat_bytes)) {
          accumulator.finish(StatisticsSectionId::kChunks,
                             SectionStatus::kOverflow, false,
                             SectionScope::kVerifiedPrefix,
                             "compressed byte total overflow");
          stopped = true;
          break;
        }
        any_idat = any_idat || type_view == "IDAT";
      }
      if (!stopped) {
        accumulator.finish(StatisticsSectionId::kChunks, SectionStatus::kReady,
                           true, SectionScope::kWholeDocument);
        compressed_bytes = idat_bytes;
        has_compressed = any_idat;
      }
    }
  }

  if (sources.stages != nullptr && !cancelled_stop) {
    const auto& stages = *sources.stages;
    if (!stages.success) {
      accumulator.finish(StatisticsSectionId::kFilters,
                         SectionStatus::kInvalidInput, false,
                         SectionScope::kNone,
                         "cannot collect statistics from failed stage analysis");
    } else if (stages.scanlines.size() > limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kFilters,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget exceeded while adapting filters");
    } else {
      std::size_t index = 0;
      bool stopped = false;
      for (const auto& scanline : stages.scanlines) {
        if (cancelled(should_cancel, index++)) {
          accumulator.finish(StatisticsSectionId::kFilters,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             "statistics adaptation cancelled");
          cancelled_stop = true;
          stopped = true;
          break;
        }
        std::uint64_t end = 0;
        if (scanline.length == 0 ||
            !checked_add(scanline.offset, scanline.length, &end) ||
            end > static_cast<std::uint64_t>(stages.filtered.size())) {
          accumulator.finish(StatisticsSectionId::kFilters,
                             SectionStatus::kInvalidInput, false,
                             SectionScope::kVerifiedPrefix,
                             "filtered scanline range is outside its backing buffer");
          stopped = true;
          break;
        }
        const auto filter = std::to_integer<unsigned char>(
            stages.filtered[static_cast<std::size_t>(scanline.offset)]);
        if (!accumulator.add(
                pnga::statistics::FilterSample{filter, scanline.length - 1})) {
          stopped = true;
          break;
        }
      }
      if (!stopped) {
        accumulator.finish(StatisticsSectionId::kFilters,
                           SectionStatus::kReady, true,
                           SectionScope::kWholeDocument);
      }
    }
  }

  if (sources.blocks != nullptr && !cancelled_stop) {
    const auto& block_index = *sources.blocks;
    if (!block_index.success) {
      accumulator.finish(StatisticsSectionId::kBlocks,
                         SectionStatus::kInvalidInput, false,
                         SectionScope::kNone,
                         "cannot collect statistics from failed block index");
    } else if (block_index.blocks.size() > limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kBlocks,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget exceeded while adapting blocks");
    } else {
      std::size_t index = 0;
      bool stopped = false;
      for (const auto& block : block_index.blocks) {
        if (cancelled(should_cancel, index++)) {
          accumulator.finish(StatisticsSectionId::kBlocks,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             "statistics adaptation cancelled");
          cancelled_stop = true;
          stopped = true;
          break;
        }
        if (block.input_bit_end < block.input_bit_begin ||
            block.output_end < block.output_begin) {
          accumulator.finish(StatisticsSectionId::kBlocks,
                             SectionStatus::kInvalidInput, false,
                             SectionScope::kVerifiedPrefix,
                             "Deflate block range is inverted");
          stopped = true;
          break;
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
        if (!accumulator.add(pnga::statistics::BlockSample{
                kind, block.input_bit_end - block.input_bit_begin,
                block.output_end - block.output_begin})) {
          stopped = true;
          break;
        }
      }
      if (!stopped) {
        accumulator.finish(StatisticsSectionId::kBlocks, SectionStatus::kReady,
                           true, SectionScope::kWholeDocument);
        inflated_bytes = block_index.total_output_bytes;
        has_inflated = true;
      }
    }
  }

  if (sources.tokens != nullptr && !cancelled_stop) {
    const auto& token_result = *sources.tokens;
    const auto finish_token_group = [&](SectionStatus status, bool complete,
                                        SectionScope scope,
                                        const char* message) {
      for (const StatisticsSectionId id :
           {StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
            StatisticsSectionId::kDistances}) {
        accumulator.finish(id, status, complete, scope, message);
      }
    };
    if (!token_result.success) {
      finish_token_group(SectionStatus::kInvalidInput, false,
                         SectionScope::kNone,
                         "cannot collect statistics from failed token decode");
    } else if (token_result.tokens.size() > limits.max_samples) {
      finish_token_group(SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget exceeded while adapting tokens");
    } else {
      std::size_t index = 0;
      bool stopped = false;
      for (const auto& token : token_result.tokens) {
        if (cancelled(should_cancel, index++)) {
          finish_token_group(SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             "statistics adaptation cancelled");
          cancelled_stop = true;
          stopped = true;
          break;
        }
        if (token.input_bit_end < token.input_bit_begin ||
            token.output_end < token.output_begin) {
          finish_token_group(SectionStatus::kInvalidInput, false,
                             SectionScope::kVerifiedPrefix,
                             "Deflate token range is inverted");
          stopped = true;
          break;
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
        if (!accumulator.add(pnga::statistics::TokenSample{
                kind, token.input_bit_end - token.input_bit_begin,
                token.output_end - token.output_begin, token.length,
                token.distance})) {
          // The accumulator already sealed tokens, lengths and distances.
          stopped = true;
          break;
        }
      }
      if (!stopped) {
        finish_token_group(SectionStatus::kReady, true,
                           SectionScope::kWholeDocument, "");
        inflated_bytes = token_result.output_bytes;
        has_inflated = true;
      }
    }
  }

  // The compression totals are a pair: both values must be fully verified
  // before the overview reports them (frozen pair semantics — a half-known
  // pair never becomes a ready zero). Without both, the overview mirrors
  // the blocking phase's terminal state or stays unavailable.
  if (has_compressed && has_inflated) {
    accumulator.set_compression_totals(compressed_bytes, inflated_bytes);
    accumulator.finish(StatisticsSectionId::kOverview, SectionStatus::kReady,
                       true, SectionScope::kWholeDocument);
  } else if (!cancelled_stop) {
    const auto& snapshot = accumulator.snapshot();
    if (snapshot.chunks.state.status != SectionStatus::kReady) {
      accumulator.finish(StatisticsSectionId::kOverview,
                         snapshot.chunks.state.status, false,
                         SectionScope::kNone, snapshot.chunks.state.error);
    } else if (sources.blocks != nullptr &&
               snapshot.blocks.state.status != SectionStatus::kReady) {
      accumulator.finish(StatisticsSectionId::kOverview,
                         snapshot.blocks.state.status, false,
                         SectionScope::kNone, snapshot.blocks.state.error);
    } else if (sources.tokens != nullptr &&
               snapshot.tokens.state.status != SectionStatus::kReady) {
      accumulator.finish(StatisticsSectionId::kOverview,
                         snapshot.tokens.state.status, false,
                         SectionScope::kNone, snapshot.tokens.state.error);
    }
  }

  return accumulator.snapshot();
}

}  // namespace pnga::analysis_engine
