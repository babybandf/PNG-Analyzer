// WP-APNG-INSPECT T05: frame statistics collection and frame-scoped
// occurrence navigation (contracts C3 + C7). The collector reuses the
// shared accumulator with frame-scoped chunk coverage; the occurrence
// entry navigates the frame stream and attaches the frame identity to
// image coordinates.

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/frame_analysis.h>
#include <pnga/analysis-engine/frame_statistics.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/statistics_occurrence_query.h>

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/selection.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "apng_inspection_fixture.h"
#include "test_png_helpers.h"

using namespace pnga::analysis_engine;
using pnga::io::MemoryByteSource;
using pnga::statistics::DocumentIdentity;
using pnga::statistics::FrameStatistics;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsSnapshot;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::ImageCoordinate;

namespace {

DocumentIdentity test_identity() {
  return DocumentIdentity{1234, "fnv1a64-v1:0123456789abcdef"};
}

// Test-local adapter mirroring the production VirtualIdatSource bridge.
class StaticIdatSource final : public pnga::io::IByteSource {
 public:
  StaticIdatSource(const pnga::png_format::VirtualIDATStream& stream,
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

StatisticsNavigationRequest occurrence_request(
    StatisticsBucketDomain domain, std::string key) {
  StatisticsNavigationRequest request;
  request.generation = 7;
  request.domain = domain;
  request.key = std::move(key);
  return request;
}

}  // namespace

TEST_CASE("Frame statistics collect the frozen byte accounting",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);
  auto analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);

  FrameStatisticsRequest stats_request;
  stats_request.target = target.target;
  stats_request.frame = analyzed.frame;
  stats_request.document = test_identity();

  const auto result = collect_frame_statistics(stats_request, nullptr);
  REQUIRE(result.error.empty());
  REQUIRE(result.key.generation == 7);
  REQUIRE(std::holds_alternative<AnimationFrame>(result.key.identity));

  // 2x3 RGBA8 frame: payload = stream size (zlib wrapper included),
  // inflated = 3 * (1 + 8) = 27, overhead = fcTL 38 + one fdAT 16 = 54.
  const auto& value = result.value;
  REQUIRE(value.payload_bytes == target.target->stream->size());
  REQUIRE(value.payload_bytes > 0);
  REQUIRE(value.inflated_bytes == 27);
  REQUIRE(value.chunk_overhead_bytes == 54);
  REQUIRE(value.width == 2);
  REQUIRE(value.height == 3);
  REQUIRE(value.document.file_size == 1234);

  const auto& snapshot = value.snapshot;
  REQUIRE(snapshot.chunks.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.chunks.data.count == 2);  // fcTL + fdAT
  REQUIRE(snapshot.filters.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.filters.data.rows == 3);
  REQUIRE(snapshot.filters.data.buckets[0].rows == 3);  // filter None
  REQUIRE(snapshot.blocks.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.tokens.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.overview.state.status == SectionStatus::kReady);
  REQUIRE(snapshot.overview.data.has_compression_totals);
  REQUIRE(snapshot.overview.data.inflated_bytes == 27);
  REQUIRE(snapshot.complete());
}

TEST_CASE("Frame statistics reject mismatched identities and budgets",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);
  auto analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);

  FrameStatisticsRequest stats_request;
  stats_request.target = target.target;
  stats_request.frame = analyzed.frame;
  stats_request.document = test_identity();

  {
    FrameStatisticsRequest mismatched = stats_request;
    auto altered = std::make_shared<FrameStageSet>(*analyzed.frame);
    altered->identity = AnimationFrame{1};
    mismatched.frame = altered;
    const auto result = collect_frame_statistics(mismatched, nullptr);
    REQUIRE(result.error == "frame identity does not match the analysis target");
  }
  {
    FrameStatisticsRequest no_budget = stats_request;
    no_budget.max_working_bytes = 0;
    const auto result = collect_frame_statistics(no_budget, nullptr);
    REQUIRE_FALSE(result.error.empty());
  }
  {
    CancellationToken token;
    token.request_cancel();
    const auto result = collect_frame_statistics(stats_request, &token);
    REQUIRE(result.error == "frame statistics cancelled");
  }
}

TEST_CASE("Frame statistics progress uses the injected clock seam",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);
  auto analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);

  FrameStatisticsRequest stats_request;
  stats_request.target = target.target;
  stats_request.frame = analyzed.frame;
  stats_request.document = test_identity();

  std::uint64_t clock_ms = 1000;
  stats_request.monotonic_millis = [&clock_ms] { return clock_ms; };

  std::size_t published = 0;
  collect_frame_statistics(stats_request, nullptr,
                           [&](const FrameStatisticsResult&) { ++published; });
  // Stage-boundary publishes are throttled: first unconditional, later
  // ones need at least 100 ms on the injected clock; a frozen clock
  // yields exactly one publish.
  REQUIRE(published == 1);

  // A clock advancing 60 ms per query crosses the 100 ms threshold between
  // stage boundaries, so at least one throttled publish fires.
  clock_ms = 1000;
  stats_request.monotonic_millis = [&clock_ms] {
    clock_ms += 60;
    return clock_ms;
  };
  published = 0;
  collect_frame_statistics(stats_request, nullptr,
                           [&](const FrameStatisticsResult&) { ++published; });
  REQUIRE(published >= 2);
}

TEST_CASE("Dual wrapped payloads produce matching scalar counts",
          "[apng-inspect]") {
  const auto dual = pnga_test::make_dual_wrapped_payload();
  REQUIRE_FALSE(dual.static_png.empty());

  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);
  auto analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);

  FrameStatisticsRequest stats_request;
  stats_request.target = target.target;
  stats_request.frame = analyzed.frame;
  stats_request.document = test_identity();
  const auto frame_stats = collect_frame_statistics(stats_request, nullptr);
  REQUIRE(frame_stats.error.empty());

  // Static wrapper over the same payload: block and token counts must
  // match the frame collection (identical logical stream).
  MemoryByteSource static_source(dual.static_png);
  const auto chunks = pnga::png_format::index_chunks(static_source);
  const pnga::png_format::VirtualIDATStream static_stream(chunks);
  const StaticIdatSource logical(static_stream, static_source);
  const auto static_blocks =
      pnga::deflate_index::index_blocks(logical, 1u << 20);
  REQUIRE(static_blocks.success);
  pnga::deflate_trace::TokenScanOptions scan_options;
  scan_options.max_output_bytes = 1u << 20;
  const auto static_scan =
      pnga::deflate_trace::scan_tokens(logical, scan_options);
  REQUIRE(static_scan.status == pnga::deflate_trace::TokenScanStatus::kReady);

  const auto stages = analyze_source(static_source);
  REQUIRE(stages.success);
  REQUIRE(frame_stats.value.snapshot.tokens.data.count ==
          static_scan.token_count);
  REQUIRE(frame_stats.value.snapshot.blocks.data.count ==
          static_blocks.blocks.size());
  REQUIRE(frame_stats.value.snapshot.filters.data.rows ==
          stages.scanlines.size());
  REQUIRE(frame_stats.value.snapshot.filters.data.buckets[0].rows ==
          stages.scanlines.size());
}

TEST_CASE("Frame occurrence navigation covers the frame stream",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);
  auto analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);
  auto block_index =
      pnga::deflate_index::index_blocks(*target.target->stream, 1u << 20);
  REQUIRE(block_index.success);

  // Filter domain: identity-carrying, canvas-global row conversion.
  const auto filter = query_frame_statistics_occurrence(
      *target.target, &analyzed.frame->stages, &block_index,
      occurrence_request(StatisticsBucketDomain::kFilterType, "0"), nullptr);
  REQUIRE(filter.status == OccurrenceStatus::kReady);
  REQUIRE(filter.selection.image.has_value());
  REQUIRE(std::holds_alternative<AnimationFrame>(
      filter.selection.image->identity));
  // Frame-local row hint 0 (first scanline) converts to the canvas row.
  REQUIRE(filter.selection.image->row == 20);

  // Token domain: ready with identity-carrying coordinates.
  const auto token = query_frame_statistics_occurrence(
      *target.target, &analyzed.frame->stages, &block_index,
      occurrence_request(StatisticsBucketDomain::kTokenKind, "literal"),
      nullptr);
  REQUIRE(token.status == OccurrenceStatus::kReady);
  REQUIRE(token.selection.image.has_value());
  REQUIRE(std::holds_alternative<AnimationFrame>(
      token.selection.image->identity));

  // Block domain: the frame's compressed payload block (the block type is
  // taken from the verified index instead of assuming a type).
  REQUIRE_FALSE(block_index.blocks.empty());
  const char* block_key = block_index.blocks.front().type ==
                                  pnga::deflate_index::BlockType::kStored
                              ? "stored"
                              : (block_index.blocks.front().type ==
                                         pnga::deflate_index::BlockType::kFixed
                                     ? "fixed"
                                     : "dynamic");
  const auto block = query_frame_statistics_occurrence(
      *target.target, &analyzed.frame->stages, &block_index,
      occurrence_request(StatisticsBucketDomain::kBlockType, block_key),
      nullptr);
  REQUIRE(block.status == OccurrenceStatus::kReady);
  REQUIRE_FALSE(block.selection.physical_spans.empty());

  // Chunk domain: the frame's fdAT chunks, with File-domain spans.
  const auto chunk = query_frame_statistics_occurrence(
      *target.target, nullptr, nullptr,
      occurrence_request(StatisticsBucketDomain::kChunkType, "fdAT"), nullptr);
  REQUIRE(chunk.status == OccurrenceStatus::kReady);
  REQUIRE(chunk.selection.physical_spans.size() == 3);  // header, data, CRC

  // The owning fcTL is derived from the first fdAT span's fixed 38-byte
  // chunk layout and anchored at its verified physical position.
  const auto fctl = query_frame_statistics_occurrence(
      *target.target, nullptr, nullptr,
      occurrence_request(StatisticsBucketDomain::kChunkType, "fcTL"), nullptr);
  REQUIRE(fctl.status == OccurrenceStatus::kReady);
  REQUIRE(fctl.selection.physical_spans.size() == 3);
  REQUIRE(fctl.selection.physical_spans[0].length == 8);   // envelope header
  REQUIRE(fctl.selection.physical_spans[1].length == 26);  // fcTL data
  REQUIRE(fctl.selection.physical_spans[2].length == 4);   // CRC
  // The fcTL data starts 12 (fdAT envelope) + 38 (fcTL chunk) bytes before
  // the fdAT data, plus the 8-byte fcTL header offset.
  {
    std::vector<pnga::png_format::PhysicalRange> payload_spans;
    REQUIRE(target.target->stream->logical_to_physical(
        0, target.target->stream->size(), payload_spans));
    REQUIRE_FALSE(payload_spans.empty());
    const std::uint64_t fdat_data = payload_spans.front().offset;
    REQUIRE(fctl.selection.physical_spans[1].offset == fdat_data - 12 - 38 + 8);
  }

  const auto foreign = query_frame_statistics_occurrence(
      *target.target, nullptr, nullptr,
      occurrence_request(StatisticsBucketDomain::kChunkType, "IHDR"), nullptr);
  REQUIRE(foreign.status == OccurrenceStatus::kNotFound);
}
