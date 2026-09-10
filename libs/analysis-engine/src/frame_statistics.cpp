#include "pnga/analysis-engine/frame_statistics.h"

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <array>
#include <chrono>
#include <utility>
#include <vector>

namespace pnga::analysis_engine {
namespace {

constexpr std::uint64_t kProgressIntervalMs = 100;
constexpr std::uint64_t kCheckInterval = 256;

// One data chunk occupies 12 bytes of chunk envelope (length + type + CRC);
// fdAT additionally carries a 4-byte sequence number inside its data, which
// is payload, not envelope. The owning fcTL occupies its 12-byte envelope
// plus 26 data bytes = 38 total. Shared chunks (IHDR/PLTE/tRNS/...) are not
// counted.
constexpr std::uint64_t kChunkEnvelopeBytes = 12;
constexpr std::uint64_t kFctlTotalBytes = 38;

bool chunk_type_is(const std::array<std::byte, 4>& type, const char* expected) {
  for (std::size_t i = 0; i < type.size(); ++i) {
    if (std::to_integer<unsigned char>(type[i]) !=
        static_cast<unsigned char>(expected[i])) {
      return false;
    }
  }
  return true;
}

bool read_chunk_type(const pnga::io::IByteSource& source,
                     std::uint64_t span_offset,
                     std::array<std::byte, 4>* type) {
  // Frame spans start after the fdAT sequence number when present, so the
  // chunk type sits either 4 bytes (IDAT) or 8 bytes (fdAT) before the
  // span. Both candidates are read and matched explicitly.
  if (span_offset < 8) {
    return false;
  }
  std::array<std::byte, 8> window{};
  if (!source.read(span_offset - 8, window.data(), window.size())) {
    return false;
  }
  std::array<std::byte, 4> idat_candidate{};
  std::array<std::byte, 4> fdat_candidate{};
  for (std::size_t i = 0; i < 4; ++i) {
    idat_candidate[i] = window[i + 4];
    fdat_candidate[i] = window[i];
  }
  if (chunk_type_is(idat_candidate, "IDAT") ||
      chunk_type_is(idat_candidate, "fdAT")) {
    *type = idat_candidate;
    return true;
  }
  if (chunk_type_is(fdat_candidate, "fdAT")) {
    *type = fdat_candidate;
    return true;
  }
  return false;
}

// Checked sum of height * filter_row_bytes over the non-empty passes.
bool inflated_total(const pnga::png_reconstruction::ScanlineLayout& layout,
                    std::uint64_t* out) noexcept {
  std::uint64_t total = 0;
  for (const auto& pass : layout.passes) {
    if (pass.height == 0) {
      continue;
    }
    if (pass.height > std::numeric_limits<std::uint64_t>::max() /
                          pass.filter_row_bytes) {
      return false;
    }
    const std::uint64_t pass_bytes = pass.height * pass.filter_row_bytes;
    if (pass_bytes > std::numeric_limits<std::uint64_t>::max() - total) {
      return false;
    }
    total += pass_bytes;
  }
  *out = total;
  return true;
}

}  // namespace

FrameStatisticsResult collect_frame_statistics(
    const FrameStatisticsRequest& request, const CancellationToken* cancellation,
    FrameStatisticsProgress on_progress) {
  FrameStatisticsResult result;
  const auto cancelled = [cancellation]() {
    return cancellation != nullptr && cancellation->cancelled();
  };
  if (cancelled()) {
    result.error = "frame statistics cancelled";
    return result;
  }
  if (!request.target || !request.target->stream || !request.frame) {
    result.error = "frame statistics require a target and analyzed frame";
    return result;
  }
  result.key = request.target->key;
  if (!(request.frame->identity == request.target->key.identity)) {
    result.error = "frame identity does not match the analysis target";
    return result;
  }
  if (request.max_working_bytes == 0) {
    result.error = "frame statistics working budget is zero";
    return result;
  }

  pnga::statistics::FrameStatistics& value = result.value;
  value.document = request.document;
  value.identity = request.target->key.identity;
  value.width = request.frame->control.width;
  value.height = request.frame->control.height;

  std::uint64_t last_publish_ms = 0;
  bool published_any = false;
  const auto publish = [&]() {
    if (!on_progress) {
      return;
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
      on_progress(result);
    }
  };

  // 1. Payload bytes and the frame's own chunk overhead from the physical
  //    span layout. Each span corresponds to one data chunk of the frame.
  std::vector<pnga::png_format::PhysicalRange> payload_spans;
  const std::uint64_t stream_size = request.target->stream->size();
  if (!request.target->stream->logical_to_physical(0, stream_size,
                                                   payload_spans)) {
    result.error = "frame payload mapping failed";
    return result;
  }
  value.payload_bytes = stream_size;

  std::uint64_t overhead = 0;
  bool overhead_verified = true;
  if (payload_spans.empty()) {
    overhead_verified = false;
  } else {
    overhead = kFctlTotalBytes;
    for (const auto& span : payload_spans) {
      std::array<std::byte, 4> type{};
      if (!read_chunk_type(*request.target->source, span.offset, &type)) {
        overhead_verified = false;
        break;
      }
      if (chunk_type_is(type, "IDAT")) {
        overhead += kChunkEnvelopeBytes;
      } else if (chunk_type_is(type, "fdAT")) {
        overhead += kChunkEnvelopeBytes + 4;  // sequence number overhead
      } else {
        overhead_verified = false;
        break;
      }
    }
  }
  if (overhead_verified) {
    value.chunk_overhead_bytes = overhead;
  }

  // 2. Inflated bytes: checked sum of height * (1 + row_bytes) over the
  //    non-empty Adam7 passes of the frame header.
  std::uint64_t inflated = 0;
  bool inflated_verified = false;
  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(request.target->header);
  if (layout.has_value() && inflated_total(*layout, &inflated)) {
    inflated_verified = true;
    value.inflated_bytes = inflated;
  }

  // 3. Frame-scoped scalar statistics through the shared accumulator.
  pnga::statistics::StatisticsAccumulator accumulator(request.limits);
  const auto finish_one = [&](pnga::statistics::StatisticsSectionId id,
                              pnga::statistics::SectionStatus status,
                              bool complete,
                              pnga::statistics::SectionScope scope,
                              const char* error) {
    accumulator.finish(id, status, complete, scope, error);
  };

  // Chunks: the owning fcTL plus the frame's own data chunks only.
  if (cancelled()) {
    finish_one(pnga::statistics::StatisticsSectionId::kChunks,
               pnga::statistics::SectionStatus::kCancelled, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               "frame statistics cancelled");
  } else if (!overhead_verified) {
    finish_one(pnga::statistics::StatisticsSectionId::kChunks,
               pnga::statistics::SectionStatus::kPartial, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               "frame chunk envelope is not fully verified");
  } else {
    bool chunk_ok = accumulator.add(pnga::statistics::ChunkSample{
        "fcTL", kFctlTotalBytes - kChunkEnvelopeBytes});
    for (std::size_t index = 0; chunk_ok && index < payload_spans.size();
         ++index) {
      if (index % kCheckInterval == 0 && cancelled()) {
        finish_one(pnga::statistics::StatisticsSectionId::kChunks,
                   pnga::statistics::SectionStatus::kCancelled, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "frame statistics cancelled");
        chunk_ok = false;
        break;
      }
      std::array<std::byte, 4> type{};
      if (!read_chunk_type(*request.target->source, payload_spans[index].offset,
                           &type)) {
        chunk_ok = false;
        break;
      }
      const bool is_idat = chunk_type_is(type, "IDAT");
      const bool is_fdat = chunk_type_is(type, "fdAT");
      if (!is_idat && !is_fdat) {
        chunk_ok = false;
        break;
      }
      // fdAT data includes the 4-byte sequence number, which is not part
      // of the frame stream payload spans.
      const std::uint64_t data_bytes =
          is_idat ? payload_spans[index].length
                  : payload_spans[index].length + 4;
      chunk_ok = accumulator.add(pnga::statistics::ChunkSample{
          is_idat ? "IDAT" : "fdAT", data_bytes});
    }
    if (chunk_ok) {
      finish_one(pnga::statistics::StatisticsSectionId::kChunks,
                 pnga::statistics::SectionStatus::kReady, true,
                 pnga::statistics::SectionScope::kWholeDocument, "");
    }
  }
  publish();

  // Filters: the frame's own scanlines.
  if (cancelled()) {
    finish_one(pnga::statistics::StatisticsSectionId::kFilters,
               pnga::statistics::SectionStatus::kCancelled, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               "frame statistics cancelled");
  } else {
    bool filter_ok = true;
    const auto& scanlines = request.frame->stages.scanlines;
    const auto& filtered = request.frame->stages.filtered;
    for (std::size_t i = 0; i < scanlines.size(); ++i) {
      if (i % kCheckInterval == 0 && cancelled()) {
        finish_one(pnga::statistics::StatisticsSectionId::kFilters,
                   pnga::statistics::SectionStatus::kCancelled, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "frame statistics cancelled");
        filter_ok = false;
        break;
      }
      const auto& span = scanlines[i];
      if (span.length == 0 || span.offset >= filtered.size()) {
        finish_one(pnga::statistics::StatisticsSectionId::kFilters,
                   pnga::statistics::SectionStatus::kInvalidInput, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "frame scanline span is invalid");
        filter_ok = false;
        break;
      }
      const auto filter = std::to_integer<std::uint8_t>(filtered[span.offset]);
      if (filter > 4) {
        finish_one(pnga::statistics::StatisticsSectionId::kFilters,
                   pnga::statistics::SectionStatus::kInvalidInput, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "invalid filter type in frame scanline");
        filter_ok = false;
        break;
      }
      if (!accumulator.add(
              pnga::statistics::FilterSample{filter, span.length - 1})) {
        filter_ok = false;  // sealed by the accumulator
        break;
      }
    }
    if (filter_ok) {
      finish_one(pnga::statistics::StatisticsSectionId::kFilters,
                 pnga::statistics::SectionStatus::kReady, true,
                 pnga::statistics::SectionScope::kWholeDocument, "");
    }
  }
  publish();

  // Blocks: one bounded scan over the frame stream only.
  auto block_index = pnga::deflate_index::index_blocks(*request.target->stream,
                                                       request.max_working_bytes);
  if (cancelled()) {
    finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
               pnga::statistics::SectionStatus::kCancelled, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               "frame statistics cancelled");
  } else if (block_index.blocks.empty()) {
    finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
               pnga::statistics::SectionStatus::kError, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               block_index.error.empty() ? "frame block index failed"
                                         : block_index.error.c_str());
  } else {
    bool block_ok = true;
    for (std::size_t index = 0; index < block_index.blocks.size(); ++index) {
      if (index % kCheckInterval == 0 && cancelled()) {
        finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
                   pnga::statistics::SectionStatus::kCancelled, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "frame statistics cancelled");
        block_ok = false;
        break;
      }
      const auto& block = block_index.blocks[index];
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
          finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
                     pnga::statistics::SectionStatus::kInvalidInput, false,
                     pnga::statistics::SectionScope::kVerifiedPrefix,
                     "unknown frame block type");
          block_ok = false;
          break;
      }
      if (!block_ok) {
        break;
      }
      if (block.input_bit_end < block.input_bit_begin ||
          block.output_end < block.output_begin) {
        finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
                   pnga::statistics::SectionStatus::kInvalidInput, false,
                   pnga::statistics::SectionScope::kVerifiedPrefix,
                   "frame block range is inverted");
        block_ok = false;
        break;
      }
      if (!accumulator.add(pnga::statistics::BlockSample{
              kind, block.input_bit_end - block.input_bit_begin,
              block.output_end - block.output_begin})) {
        block_ok = false;  // sealed by the accumulator
        break;
      }
    }
    if (block_ok) {
      finish_one(pnga::statistics::StatisticsSectionId::kBlocks,
                 block_index.success
                     ? pnga::statistics::SectionStatus::kReady
                     : pnga::statistics::SectionStatus::kPartial,
                 block_index.success,
                 block_index.success
                     ? pnga::statistics::SectionScope::kWholeDocument
                     : pnga::statistics::SectionScope::kVerifiedPrefix,
                 block_index.success
                     ? ""
                     : "frame block index is a verified prefix");
    }
  }
  publish();

  // Tokens: streaming scan over the frame stream; lengths/distances finish
  // with the token group.
  const auto finish_token_group = [&](pnga::statistics::SectionStatus status,
                                      bool complete,
                                      pnga::statistics::SectionScope scope,
                                      const char* error) {
    finish_one(pnga::statistics::StatisticsSectionId::kTokens, status, complete,
               scope, error);
    finish_one(pnga::statistics::StatisticsSectionId::kLengths, status,
               complete, scope, error);
    finish_one(pnga::statistics::StatisticsSectionId::kDistances, status,
               complete, scope, error);
  };
  {
    pnga::deflate_trace::TokenScanOptions options;
    options.max_output_bytes = request.max_working_bytes;
    options.should_cancel = cancelled;
    std::uint64_t delivered = 0;
    options.observer = [&](const pnga::deflate_trace::TokenFact& fact) {
      if (delivered % kCheckInterval == 0 && cancelled()) {
        return false;
      }
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
          return false;
      }
      ++delivered;
      return accumulator.add(pnga::statistics::TokenSample{
          kind, fact.input_bits, fact.output_bytes, fact.length,
          fact.distance});
    };
    const auto scan =
        pnga::deflate_trace::scan_tokens(*request.target->stream, options);
    switch (scan.status) {
      case pnga::deflate_trace::TokenScanStatus::kReady:
        finish_token_group(pnga::statistics::SectionStatus::kReady, true,
                           pnga::statistics::SectionScope::kWholeDocument, "");
        break;
      case pnga::deflate_trace::TokenScanStatus::kPartial:
        finish_token_group(pnga::statistics::SectionStatus::kPartial, false,
                           pnga::statistics::SectionScope::kVerifiedPrefix,
                           "frame token scan stopped early");
        break;
      case pnga::deflate_trace::TokenScanStatus::kCancelled:
        finish_token_group(pnga::statistics::SectionStatus::kCancelled, false,
                           pnga::statistics::SectionScope::kVerifiedPrefix,
                           "frame statistics cancelled");
        break;
      case pnga::deflate_trace::TokenScanStatus::kBudgetExceeded:
        finish_token_group(pnga::statistics::SectionStatus::kBudgetExceeded,
                           false,
                           pnga::statistics::SectionScope::kVerifiedPrefix,
                           "frame token scan budget exceeded");
        break;
      case pnga::deflate_trace::TokenScanStatus::kInvalidInput:
        finish_token_group(pnga::statistics::SectionStatus::kInvalidInput,
                           false,
                           pnga::statistics::SectionScope::kVerifiedPrefix,
                           "cannot collect statistics from failed token decode");
        break;
      case pnga::deflate_trace::TokenScanStatus::kError:
        finish_token_group(pnga::statistics::SectionStatus::kError, false,
                           pnga::statistics::SectionScope::kNone,
                           "failed to read the token stream");
        break;
    }
  }
  publish();

  // Overview totals only when both sides were independently verified and
  // the layout total agrees with the materialized filtered bytes.
  if (!cancelled() && overhead_verified && inflated_verified &&
      block_index.success &&
      request.frame->stages.filtered.size() == inflated) {
    if (accumulator.set_compression_totals(value.payload_bytes, inflated)) {
      finish_one(pnga::statistics::StatisticsSectionId::kOverview,
                 pnga::statistics::SectionStatus::kReady, true,
                 pnga::statistics::SectionScope::kWholeDocument, "");
    } else {
      finish_one(pnga::statistics::StatisticsSectionId::kOverview,
                 pnga::statistics::SectionStatus::kOverflow, false,
                 pnga::statistics::SectionScope::kVerifiedPrefix,
                 "frame overview totals overflow");
    }
  } else {
    finish_one(pnga::statistics::StatisticsSectionId::kOverview,
               pnga::statistics::SectionStatus::kPartial, false,
               pnga::statistics::SectionScope::kVerifiedPrefix,
               "frame overview totals are not fully verified");
  }

  value.snapshot = accumulator.snapshot();
  if (cancelled()) {
    result.error = "frame statistics cancelled";
  }
  return result;
}

}  // namespace pnga::analysis_engine
