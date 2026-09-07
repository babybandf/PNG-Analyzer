// WP-602C: whole-document statistics collection pipeline. Computes the
// document identity, feeds the cached Chunk/StageSet facts and the streamed
// Block/Token facts through StatisticsAccumulator, and publishes throttled
// immutable progress copies. The virtual IDAT stream is adapted without
// concatenation; the token pass is the WP-602C scalar scan, so no
// TokenEvent/output/table list is ever retained.

#include "pnga/analysis-engine/statistics_collector.h"

#include <pnga/analysis-engine/document_fingerprint.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/statistics/statistics.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace pnga::analysis_engine {
namespace {

using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsAccumulator;
using pnga::statistics::StatisticsSectionId;

constexpr std::uint64_t kMaxWorkingBytesCap = 64ull << 20;
constexpr std::uint64_t kProgressIntervalMs = 100;
constexpr std::uint64_t kCheckInterval = 256;
constexpr const char* kCancelledMessage = "statistics collection cancelled";

// Adapts the virtual IDAT stream to IByteSource without concatenating the
// payloads. Borrowed: the stream and the file source must outlive the
// adapter, which is guaranteed by the collection scope.
class VirtualIdatByteSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatByteSource(const pnga::png_format::VirtualIDATStream& stream,
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
  const pnga::png_format::VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *output = left + right;
  return true;
}

// Saturating per-token output bound: no reachable stream can produce more
// inflated bytes than the token sample budget can represent.
std::uint64_t output_budget_for(std::uint64_t max_samples) noexcept {
  constexpr std::uint64_t kMaxMatchBytes = 258;
  if (max_samples >
      std::numeric_limits<std::uint64_t>::max() / kMaxMatchBytes) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return max_samples * kMaxMatchBytes;
}

void finish_all(StatisticsAccumulator& accumulator, SectionStatus status,
                SectionScope scope, const char* message) {
  for (const StatisticsSectionId id :
       {StatisticsSectionId::kOverview, StatisticsSectionId::kChunks,
        StatisticsSectionId::kFilters, StatisticsSectionId::kBlocks,
        StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
        StatisticsSectionId::kDistances}) {
    accumulator.finish(id, status, false, scope, message);
  }
}

void finish_token_group(StatisticsAccumulator& accumulator,
                        SectionStatus status, bool complete,
                        SectionScope scope, const char* message) {
  for (const StatisticsSectionId id :
       {StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
        StatisticsSectionId::kDistances}) {
    accumulator.finish(id, status, complete, scope, message);
  }
}

}  // namespace

StatisticsCollectionResult collect_document_statistics(
    const StatisticsCollectionRequest& request,
    const CancellationToken* cancellation,
    StatisticsProgressCallback on_progress) {
  StatisticsCollectionResult result;
  result.generation = request.generation;
  StatisticsAccumulator accumulator(request.limits);

  // Returns the result with a fresh value copy of the accumulator snapshot;
  // the callback never sees mutable accumulator storage.
  const auto take_result = [&]() {
    result.snapshot = accumulator.snapshot();
    return result;
  };
  const auto cancelled = [&]() {
    return cancellation != nullptr && cancellation->cancelled();
  };

  // Progress throttling: the first publication is unconditional, later ones
  // are gated to no more than one per 100 ms on the injected monotonic
  // clock. Callbacks never run under a decoder or scheduler lock (the
  // collection is synchronous and lock-free).
  std::uint64_t last_publish_ms = 0;
  bool published_any = false;
  const auto throttle_allows = [&]() {
    if (!on_progress) {
      return false;
    }
    const std::uint64_t now =
        request.monotonic_millis
            ? request.monotonic_millis()
            : static_cast<std::uint64_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
    if (!published_any || now - last_publish_ms >= kProgressIntervalMs) {
      last_publish_ms = now;
      published_any = true;
      return true;
    }
    return false;
  };
  const auto publish = [&](StatisticsSectionId section, std::uint64_t processed,
                           std::optional<std::uint64_t> total) {
    StatisticsCollectionResult copy;
    copy.generation = request.generation;
    copy.document = result.document;
    copy.snapshot = accumulator.snapshot();
    const StatisticsProgress progress{section, processed, std::move(total)};
    on_progress(copy, progress);
  };
  const auto publish_throttled = [&](StatisticsSectionId section,
                                     std::uint64_t processed,
                                     std::optional<std::uint64_t> total) {
    if (throttle_allows()) {
      publish(section, processed, std::move(total));
    }
  };

  // Invalid requests are rejected before any source access.
  if (request.max_working_bytes == 0 ||
      request.max_working_bytes > kMaxWorkingBytesCap) {
    finish_all(accumulator, SectionStatus::kBudgetExceeded,
               SectionScope::kNone,
               "declared working memory exceeds the 64 MiB cap");
    return take_result();
  }
  if (request.limits.max_samples == 0 ||
      request.limits.max_chunk_types == 0 ||
      request.limits.max_length_values == 0 ||
      request.limits.max_distance_values == 0) {
    finish_all(accumulator, SectionStatus::kBudgetExceeded,
               SectionScope::kNone,
               "statistics bucket budget must be positive");
    return take_result();
  }

  // 1. Document identity (cancelable, 64 KiB windows).
  if (cancelled()) {
    finish_all(accumulator, SectionStatus::kCancelled, SectionScope::kNone,
               kCancelledMessage);
    return take_result();
  }
  std::string fingerprint_error;
  auto document =
      compute_document_identity(*request.source, cancellation, &fingerprint_error);
  if (!document) {
    if (cancelled()) {
      finish_all(accumulator, SectionStatus::kCancelled, SectionScope::kNone,
                 kCancelledMessage);
    } else {
      finish_all(accumulator, SectionStatus::kError, SectionScope::kNone,
                 "failed to compute the document identity");
    }
    return take_result();
  }
  result.document = *document;

  // Only cooperative cancellation stops the phase chain; lower-level
  // failures seal their own sections and later phases still run, matching
  // the frozen adapter semantics for independent sources.
  bool cancelled_stop = false;
  bool chunks_ready = false;
  std::uint64_t compressed_bytes = 0;
  bool has_compressed = false;
  bool blocks_ready = false;
  std::uint64_t inflated_bytes = 0;
  bool has_inflated = false;

  // 2. Overview progress is published first, right after the identity.
  if (throttle_allows()) {
    publish(StatisticsSectionId::kOverview, 0, std::nullopt);
  }

  // 3. Chunks section: cached envelope index scalars.
  {
    std::uint64_t processed = 0;
    if (request.chunks.chunks.size() > request.limits.max_samples) {
      accumulator.finish(StatisticsSectionId::kChunks,
                         SectionStatus::kBudgetExceeded, false,
                         SectionScope::kNone,
                         "statistics sample budget exceeded");
    } else {
      std::uint64_t index = 0;
      bool stopped = false;
      for (const auto& chunk : request.chunks.chunks) {
        if (index % kCheckInterval == 0 && cancelled()) {
          accumulator.finish(StatisticsSectionId::kChunks,
                             SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             kCancelledMessage);
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
          stopped = true;  // sealed by the accumulator
          break;
        }
        ++processed;
        if (type_view == "IDAT") {
          if (!checked_add(compressed_bytes, chunk.data_length,
                           &compressed_bytes)) {
            accumulator.finish(StatisticsSectionId::kChunks,
                               SectionStatus::kOverflow, false,
                               SectionScope::kVerifiedPrefix,
                               "compressed byte total overflow");
            stopped = true;
            break;
          }
          has_compressed = true;
        }
        ++index;
      }
      if (!stopped) {
        accumulator.finish(StatisticsSectionId::kChunks,
                           SectionStatus::kReady, true,
                           SectionScope::kWholeDocument, "");
        chunks_ready = true;
      }
    }
    publish_throttled(StatisticsSectionId::kChunks, processed,
                      request.chunks.chunks.size());
  }

  // 4. Filters section: cached StageSet scanline facts. A missing source
  // stays unavailable (never a ready zero value).
  {
    std::uint64_t processed = 0;
    if (request.stages != nullptr && !cancelled_stop) {
      const auto& stages = *request.stages;
      if (!stages.success) {
        accumulator.finish(StatisticsSectionId::kFilters,
                           SectionStatus::kInvalidInput, false,
                           SectionScope::kNone,
                           "cannot collect statistics from failed stage "
                           "analysis");
      } else if (stages.scanlines.size() > request.limits.max_samples) {
        accumulator.finish(StatisticsSectionId::kFilters,
                           SectionStatus::kBudgetExceeded, false,
                           SectionScope::kNone,
                           "statistics sample budget exceeded");
      } else {
        std::uint64_t index = 0;
        bool stopped = false;
        for (const auto& scanline : stages.scanlines) {
          if (index % kCheckInterval == 0 && cancelled()) {
            accumulator.finish(StatisticsSectionId::kFilters,
                               SectionStatus::kCancelled, false,
                               SectionScope::kVerifiedPrefix,
                               kCancelledMessage);
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
                               "filtered scanline range is outside its "
                               "backing buffer");
            stopped = true;
            break;
          }
          const auto filter = std::to_integer<unsigned char>(
              stages.filtered[static_cast<std::size_t>(scanline.offset)]);
          if (!accumulator.add(pnga::statistics::FilterSample{
                  filter, scanline.length - 1})) {
            stopped = true;  // sealed by the accumulator
            break;
          }
          ++processed;
          ++index;
        }
        if (!stopped) {
          accumulator.finish(StatisticsSectionId::kFilters,
                             SectionStatus::kReady, true,
                             SectionScope::kWholeDocument, "");
        }
      }
      publish_throttled(StatisticsSectionId::kFilters, processed,
                        stages.scanlines.size());
    }
  }

  // 5. Blocks section: one index scan over the virtual IDAT stream.
  const std::uint64_t output_budget =
      output_budget_for(request.limits.max_samples);
  if (!cancelled_stop) {
    std::uint64_t processed = 0;
    std::optional<std::uint64_t> total;
    if (cancelled()) {
      for (const StatisticsSectionId id :
           {StatisticsSectionId::kBlocks, StatisticsSectionId::kTokens,
            StatisticsSectionId::kLengths, StatisticsSectionId::kDistances}) {
        accumulator.finish(id, SectionStatus::kCancelled, false,
                           SectionScope::kNone, kCancelledMessage);
      }
      cancelled_stop = true;
    } else {
      pnga::png_format::VirtualIDATStream stream(request.chunks);
      VirtualIdatByteSource logical(stream, *request.source);
      const auto blocks = pnga::deflate_index::index_blocks(logical, output_budget);
      total = blocks.blocks.size();
      processed = blocks.blocks.size();
      if (cancelled()) {
        // The scan completed but the collection was stopped meanwhile; the
        // indexed blocks remain a verified prefix.
        accumulator.finish(StatisticsSectionId::kBlocks,
                           SectionStatus::kCancelled, false,
                           SectionScope::kVerifiedPrefix, kCancelledMessage);
        for (const StatisticsSectionId id :
             {StatisticsSectionId::kTokens, StatisticsSectionId::kLengths,
              StatisticsSectionId::kDistances}) {
          accumulator.finish(id, SectionStatus::kCancelled, false,
                             SectionScope::kNone, kCancelledMessage);
        }
        cancelled_stop = true;
      } else if (!blocks.success) {
        accumulator.finish(StatisticsSectionId::kBlocks,
                           SectionStatus::kInvalidInput, false,
                           SectionScope::kNone,
                           "cannot collect statistics from failed block index");
      } else if (blocks.blocks.size() > request.limits.max_samples) {
        accumulator.finish(StatisticsSectionId::kBlocks,
                           SectionStatus::kBudgetExceeded, false,
                           SectionScope::kNone,
                           "statistics sample budget exceeded");
      } else {
        std::uint64_t index = 0;
        bool stopped = false;
        for (const auto& block : blocks.blocks) {
          if (index % kCheckInterval == 0 && cancelled()) {
            accumulator.finish(StatisticsSectionId::kBlocks,
                               SectionStatus::kCancelled, false,
                               SectionScope::kVerifiedPrefix,
                               kCancelledMessage);
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
            default:
              accumulator.finish(StatisticsSectionId::kBlocks,
                                 SectionStatus::kInvalidInput, false,
                                 SectionScope::kVerifiedPrefix,
                                 "invalid Deflate block kind");
              stopped = true;
              break;
          }
          if (stopped) {
            break;
          }
          if (!accumulator.add(pnga::statistics::BlockSample{
                  kind, block.input_bit_end - block.input_bit_begin,
                  block.output_end - block.output_begin})) {
            stopped = true;  // sealed by the accumulator
            break;
          }
          ++index;
        }
        if (!stopped) {
          accumulator.finish(StatisticsSectionId::kBlocks,
                             SectionStatus::kReady, true,
                             SectionScope::kWholeDocument, "");
          blocks_ready = true;
          inflated_bytes = blocks.total_output_bytes;
          has_inflated = true;
        }
      }
    }
    publish_throttled(StatisticsSectionId::kBlocks, processed,
                      std::move(total));
  }

  // 6. Token section: the streaming scalar scan with the accumulator as the
  // aggregate-then-discard observer. No TokenEvent/output/table list exists.
  if (!cancelled_stop) {
    std::uint64_t facts_seen = 0;
    if (cancelled()) {
      finish_token_group(accumulator, SectionStatus::kCancelled, false,
                         SectionScope::kNone, kCancelledMessage);
      cancelled_stop = true;
    } else {
      pnga::png_format::VirtualIDATStream stream(request.chunks);
      VirtualIdatByteSource logical(stream, *request.source);
      pnga::deflate_trace::TokenScanOptions scan_options;
      scan_options.max_output_bytes = output_budget;
      scan_options.max_tokens = request.limits.max_samples;
      scan_options.should_cancel = [&cancelled] { return cancelled(); };
      scan_options.observer = [&](const pnga::deflate_trace::TokenFact& fact) {
        ++facts_seen;
        pnga::statistics::TokenKind kind;
        switch (fact.kind) {
          case pnga::deflate_trace::TokenKind::kLiteral:
            kind = pnga::statistics::TokenKind::kLiteral;
            break;
          case pnga::deflate_trace::TokenKind::kLengthDistance:
            kind = pnga::statistics::TokenKind::kLengthDistance;
            break;
          case pnga::deflate_trace::TokenKind::kEndOfBlock:
            kind = pnga::statistics::TokenKind::kEndOfBlock;
            break;
          default:
            finish_token_group(accumulator, SectionStatus::kInvalidInput,
                               false, SectionScope::kVerifiedPrefix,
                               "invalid Deflate token kind");
            return false;
        }
        if (!accumulator.add(pnga::statistics::TokenSample{
                kind, fact.input_bits, fact.output_bytes, fact.length,
                fact.distance})) {
          return false;  // sealed by the accumulator
        }
        if (facts_seen % kCheckInterval == 0) {
          if (cancelled()) {
            finish_token_group(accumulator, SectionStatus::kCancelled, false,
                               SectionScope::kVerifiedPrefix,
                               kCancelledMessage);
            return false;
          }
          publish_throttled(StatisticsSectionId::kTokens, facts_seen,
                            std::nullopt);
        }
        return true;
      };
      const auto scan = pnga::deflate_trace::scan_tokens(logical, scan_options);
      switch (scan.status) {
        case pnga::deflate_trace::TokenScanStatus::kReady:
          finish_token_group(accumulator, SectionStatus::kReady, true,
                             SectionScope::kWholeDocument, "");
          inflated_bytes = scan.output_bytes;
          has_inflated = true;
          break;
        case pnga::deflate_trace::TokenScanStatus::kPartial:
          // The observer only stops for a sealed accumulator or a
          // cancellation; the earlier terminal state wins.
          finish_token_group(accumulator, SectionStatus::kPartial, false,
                             SectionScope::kVerifiedPrefix,
                             "statistics token scan stopped early");
          break;
        case pnga::deflate_trace::TokenScanStatus::kCancelled:
          finish_token_group(accumulator, SectionStatus::kCancelled, false,
                             SectionScope::kVerifiedPrefix,
                             kCancelledMessage);
          break;
        case pnga::deflate_trace::TokenScanStatus::kBudgetExceeded:
          finish_token_group(accumulator, SectionStatus::kBudgetExceeded,
                             false, SectionScope::kVerifiedPrefix,
                             "statistics sample budget exceeded");
          break;
        case pnga::deflate_trace::TokenScanStatus::kInvalidInput:
          // The streamed prefix stays aggregated as the verified evidence of
          // the malformed stream.
          finish_token_group(accumulator, SectionStatus::kInvalidInput, false,
                             SectionScope::kVerifiedPrefix,
                             "cannot collect statistics from failed token "
                             "decode");
          break;
        case pnga::deflate_trace::TokenScanStatus::kError:
          finish_token_group(accumulator, SectionStatus::kError, false,
                             SectionScope::kNone,
                             "failed to read the token stream");
          break;
      }
      publish_throttled(StatisticsSectionId::kTokens, facts_seen,
                        std::nullopt);
    }
  }

  // 7. Overview: the compression totals are a pair — both values must come
  // from fully verified scans before the section reports them. An unknown
  // or half-verified pair stays absent (never a ready zero value), and the
  // section carries the blocking phase's honest stop reason instead.
  if (cancelled_stop) {
    if (chunks_ready && (blocks_ready || has_inflated)) {
      accumulator.set_compression_totals(compressed_bytes, inflated_bytes);
      accumulator.finish(StatisticsSectionId::kOverview, SectionStatus::kReady,
                         true, SectionScope::kWholeDocument, "");
    } else {
      // Cancellation before both totals were verified: the pair is unknown,
      // so the overview reports the cancellation with no scope rather than
      // a partial pair.
      accumulator.finish(StatisticsSectionId::kOverview,
                         SectionStatus::kCancelled, false,
                         SectionScope::kNone, kCancelledMessage);
    }
    // Sections that never started keep no verified scope.
    for (const StatisticsSectionId id :
         {StatisticsSectionId::kBlocks, StatisticsSectionId::kTokens,
          StatisticsSectionId::kLengths, StatisticsSectionId::kDistances}) {
      accumulator.finish(id, SectionStatus::kCancelled, false,
                         SectionScope::kNone, kCancelledMessage);
    }
  } else if (has_compressed && has_inflated) {
    accumulator.set_compression_totals(compressed_bytes, inflated_bytes);
    accumulator.finish(StatisticsSectionId::kOverview, SectionStatus::kReady,
                       true, SectionScope::kWholeDocument, "");
  } else {
    // Totals unknown without cancellation: mirror the blocking phase's
    // terminal state — chunks own the compressed size, blocks the inflated
    // size. Without any compression evidence at all the overview stays
    // unavailable.
    const auto& snapshot = accumulator.snapshot();
    if (snapshot.chunks.state.status != SectionStatus::kReady) {
      accumulator.finish(StatisticsSectionId::kOverview,
                         snapshot.chunks.state.status, false,
                         SectionScope::kNone, snapshot.chunks.state.error);
    } else if (snapshot.blocks.state.status != SectionStatus::kReady) {
      accumulator.finish(StatisticsSectionId::kOverview,
                         snapshot.blocks.state.status, false,
                         SectionScope::kNone, snapshot.blocks.state.error);
    }
  }

  // Final throttled publication: the last progress snapshot a consumer sees
  // always carries the complete final section states.
  publish_throttled(StatisticsSectionId::kOverview, 0, std::nullopt);

  return take_result();
}

}  // namespace pnga::analysis_engine
