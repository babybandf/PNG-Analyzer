// WP-APNG-INSPECT T03: per-frame row indexing and reconstruction over the
// virtual compressed stream (contract C2). The coordinator opens an
// immutable AnalysisTarget, anchors are built through the generic stream
// interface and dual-wrapped payloads must produce identical logical row
// evidence with different physical file offsets.

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/filtered_scanlines.h>
#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/analysis-engine/stage_analysis.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_frame_stream.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "apng_fixture.h"
#include "apng_inspection_fixture.h"

using namespace pnga::analysis_engine;
using pnga::io::MemoryByteSource;
using pnga::png_format::FrameControl;
using pnga::png_format::VirtualIDATStream;
using pnga::png_format::index_chunks;

namespace {

// Waits until `ready_row` reaches `status` (via the callback), with a timeout.
bool wait_status(QueryCoordinator& coordinator, std::uint64_t row,
                 QueryStatus status, int timeout_ms = 5000) {
  std::mutex m;
  std::condition_variable cv;
  std::atomic<bool> done{false};
  coordinator.setStatusCallback(
      [&](std::uint64_t r, QueryStatus s) {
        if (r == row && s == status) {
          done = true;
          cv.notify_all();
        }
      });
  std::unique_lock<std::mutex> lock(m);
  return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [&] { return done.load(); });
}

// A FrameRequest over arbitrary APNG bytes with a canvas 16x32 of `format`.
FrameRequest frame_request(const std::vector<std::byte>& png,
                           std::uint32_t ordinal,
                           const pnga_test::ApngFormat& format = {}) {
  auto source = std::make_shared<const MemoryByteSource>(png);
  FrameRequest request;
  request.generation = 7;
  request.request_serial = 11;
  request.ordinal = ordinal;
  request.source = source;
  request.index = std::make_shared<const pnga::png_format::AnimationIndex>(
      pnga::png_format::index_animation(*source,
                                        pnga::png_format::AnimationLimits{},
                                        [] { return false; }));
  request.canvas_header = pnga::png_reconstruction::ImageHeader{
      16, 32, format.bit_depth, format.color_type, format.interlace};
  return request;
}

std::vector<std::byte> read_all(const pnga::png_format::IVirtualCompressedStream&
                                    stream) {
  std::vector<std::byte> out(stream.size());
  REQUIRE(stream.read(0, out.data(), out.size()));
  return out;
}

}  // namespace

TEST_CASE("QueryCoordinator opens a frame analysis target", "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto t = make_frame_target(request);
  REQUIRE(t.target);
  QueryCoordinator coordinator(/*worker_count=*/1, /*budget=*/1u << 20);
  REQUIRE(coordinator.open(t.target, 32768));
  REQUIRE(coordinator.has_index());
  REQUIRE(coordinator.scanline_count() == 3);
  REQUIRE(coordinator.anchors().header.width == 2);
  REQUIRE(coordinator.anchors().header.height == 3);
}

TEST_CASE("Frame row queries replay the requested scanline", "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto t = make_frame_target(request);
  REQUIRE(t.target);
  QueryCoordinator coordinator(1, 1u << 20);
  REQUIRE(coordinator.open(t.target, 4096));

  const FrameResult analyzed = analyze_frame(request, nullptr);
  REQUIRE(analyzed.stop == FrameResult::Stop::kReady);
  const StageSet& stages = analyzed.frame->stages;
  REQUIRE(stages.success);
  REQUIRE(coordinator.scanline_count() == stages.scanlines.size());

  const std::uint64_t row_bytes = stages.header.width * 4;
  for (std::uint64_t row = 0; row < 3; ++row) {
    REQUIRE(coordinator.query_scanline(row, JobPriority::kSelection).status ==
            QueryStatus::kReplaying);
    REQUIRE(wait_status(coordinator, row, QueryStatus::kReady));
    const auto result = coordinator.query_scanline(
        row, JobPriority::kSelection);
    REQUIRE(result.status == QueryStatus::kReady);
    REQUIRE(result.unfiltered.size() == row_bytes);
    const auto expected = std::vector<std::byte>(
        stages.unfiltered.begin() +
            static_cast<std::ptrdiff_t>(row * row_bytes),
        stages.unfiltered.begin() +
            static_cast<std::ptrdiff_t>((row + 1) * row_bytes));
    REQUIRE(result.unfiltered == expected);
  }

  const auto beyond = coordinator.query_scanline(3, JobPriority::kSelection);
  REQUIRE(beyond.status == QueryStatus::kError);
}

TEST_CASE("Frame and static wrappers share logical row evidence",
          "[apng-inspect]") {
  const auto dual = pnga_test::make_dual_wrapped_payload();
  REQUIRE_FALSE(dual.static_png.empty());

  auto request = pnga_test::inspection_request();
  auto target = make_frame_target(request);
  REQUIRE(target.target);

  // Frame side: anchors and filtered bytes through the stream interface.
  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(target.target->header);
  REQUIRE(layout.has_value());
  const auto frame_filtered = inflate_filtered(*target.target->stream, *layout);
  REQUIRE(frame_filtered.success);

  // Static side: the same payload as a VirtualIDATStream over its IDATs.
  MemoryByteSource static_source(dual.static_png);
  const auto static_index = index_chunks(static_source);
  const VirtualIDATStream static_stream(static_index);
  const auto static_filtered =
      inflate_filtered(static_stream, static_source, *layout);
  REQUIRE(static_filtered.success);

  REQUIRE(frame_filtered.filtered == static_filtered.filtered);
  REQUIRE(frame_filtered.scanlines.size() == static_filtered.scanlines.size());
  REQUIRE(frame_filtered.scanlines == static_filtered.scanlines);
  REQUIRE(frame_filtered.adler_ok == static_filtered.adler_ok);

  // Anchor build and row restore agree through both stream shapes.
  const auto frame_anchors =
      build_scanline_anchors(*target.target->stream, target.target->header,
                             1024, 1ull << 30);
  const auto static_anchors = build_scanline_anchors(
      static_stream, static_source, target.target->header, 1024, 1ull << 30);
  REQUIRE(frame_anchors.success);
  REQUIRE(static_anchors.success);
  REQUIRE(frame_anchors.anchors.size() == static_anchors.anchors.size());
  for (std::uint64_t row = 0; row < 3; ++row) {
    const auto frame_row = restore_scanline(frame_anchors, *target.target->stream, row);
    const auto static_row = restore_scanline(static_anchors, static_stream,
                                             static_source, row);
    REQUIRE(frame_row.success);
    REQUIRE(static_row.success);
    REQUIRE(frame_row.unfiltered == static_row.unfiltered);
  }

  // Physical placements differ even though logical evidence matches.
  std::vector<pnga::png_format::PhysicalRange> frame_spans;
  std::vector<pnga::png_format::PhysicalRange> static_spans;
  REQUIRE(target.target->stream->logical_to_physical(
      0, target.target->stream->size(), frame_spans));
  REQUIRE(static_stream.logical_to_physical(0, static_stream.size(),
                                            static_spans));
  REQUIRE_FALSE(frame_spans.empty());
  REQUIRE_FALSE(static_spans.empty());
  REQUIRE(frame_spans.front().offset != static_spans.front().offset);
}

TEST_CASE("Frame row matrix covers color types, depths and Adam7",
          "[apng-inspect]") {
  struct Case {
    pnga_test::ApngFormat format;
    bool palette;
  };
  const std::vector<Case> cases{
      {pnga_test::ApngFormat{0, 1}, false},  // grayscale 1-bit
      {pnga_test::ApngFormat{0, 8}, false},
      {pnga_test::ApngFormat{0, 16}, false},
      {pnga_test::ApngFormat{2, 8}, false},
      {pnga_test::ApngFormat{2, 16}, false},
      {pnga_test::ApngFormat{3, 8}, true},   // palette
      {pnga_test::ApngFormat{4, 8}, false},
      {pnga_test::ApngFormat{4, 16}, false},
      {pnga_test::ApngFormat{6, 8}, false},
      {pnga_test::ApngFormat{6, 16}, false},
      {pnga_test::ApngFormat{6, 8, /*interlace=*/true}, false},
  };

  std::vector<std::byte> palette_bytes;
  std::vector<std::byte> transparency_bytes;
  for (std::uint32_t i = 0; i < 256; ++i) {
    palette_bytes.push_back(
        std::byte{static_cast<unsigned char>((i * 37 + 12) % 256)});
    palette_bytes.push_back(
        std::byte{static_cast<unsigned char>((i * 71 + 90) % 256)});
    palette_bytes.push_back(
        std::byte{static_cast<unsigned char>((i * 13 + 200) % 256)});
    transparency_bytes.push_back(std::byte{static_cast<unsigned char>(i % 251)});
  }
  const std::vector<std::byte> empty_bytes;

  for (const auto& item : cases) {
    const std::vector<FrameControl> frames{
        FrameControl{0, 4, 3, 0, 0, 1, 100, 0, 0}};
    // PLTE/tRNS are only valid for palette images; tRNS for grayscale and
    // truecolor must be 2/6 bytes and is not exercised here.
    const bool use_palette = item.palette;
    const auto& palette = use_palette ? palette_bytes : empty_bytes;
    const auto& transparency = use_palette ? transparency_bytes : empty_bytes;
    const auto png = pnga_test::make_apng_format(
        /*default_is_frame=*/false, frames, item.format,
        /*sample_at=*/{}, palette, transparency, /*canvas_width=*/16,
        /*canvas_height=*/32);
    auto request = frame_request(png, 0, item.format);
    CAPTURE(item.format.color_type, item.format.bit_depth,
            item.format.interlace);
    REQUIRE(request.index);
    CAPTURE(request.index->status, request.index->frames.size());
    REQUIRE(request.index->status ==
            pnga::png_format::AnimationStatus::kComplete);
    auto t = make_frame_target(request);
    REQUIRE(t.target);
    const auto anchors = build_scanline_anchors(*t.target->stream,
                                                t.target->header, 512,
                                                1ull << 30);
    CAPTURE(anchors.error);
    REQUIRE(anchors.success);
    QueryCoordinator coordinator(1, 1u << 20);
    REQUIRE(coordinator.open(t.target, 512));
    REQUIRE(coordinator.scanline_count() > 0);

    // Cross-check the anchor count against the interface analyze_stages
    // kernel on the same stream.
    const StageSet stages = analyze_stages(
        *t.target->stream, t.target->header, DecodeLimits{}, nullptr);
    REQUIRE(stages.success);
    REQUIRE(coordinator.scanline_count() == stages.scanlines.size());

    const auto row = coordinator.query_scanline(0, JobPriority::kSelection);
    REQUIRE(row.status == QueryStatus::kReplaying);
    REQUIRE(wait_status(coordinator, 0, QueryStatus::kReady));
    const auto ready = coordinator.query_scanline(0, JobPriority::kSelection);
    REQUIRE(ready.status == QueryStatus::kReady);
    const auto expected_first_row = std::vector<std::byte>(
        stages.unfiltered.begin(),
        stages.unfiltered.begin() +
            static_cast<std::ptrdiff_t>(ready.unfiltered.size()));
    REQUIRE(ready.unfiltered == expected_first_row);
  }
}

TEST_CASE("Frame row queries honor budget and survive request destruction",
          "[apng-inspect]") {
  auto request = pnga_test::inspection_request();
  auto t = make_frame_target(request);
  REQUIRE(t.target);

  QueryCoordinator tiny(1, /*budget=*/1);
  REQUIRE(tiny.open(t.target, 4096));
  const auto rejected = tiny.query_scanline(1, JobPriority::kSelection);
  REQUIRE(rejected.status == QueryStatus::kError);

  // Shared ownership: the target keeps the source alive after the request
  // and its local source copy are gone.
  auto source_copy = request.source;
  request.source.reset();
  QueryCoordinator coordinator(1, 1u << 20);
  REQUIRE(coordinator.open(t.target, 4096));
  source_copy.reset();
  request.index.reset();
  const auto result0 = coordinator.query_scanline(0, JobPriority::kSelection);
  REQUIRE(result0.status == QueryStatus::kReplaying);
  REQUIRE(wait_status(coordinator, 0, QueryStatus::kReady));
  const auto result = coordinator.query_scanline(0, JobPriority::kSelection);
  REQUIRE(result.status == QueryStatus::kReady);
  REQUIRE(result.unfiltered.size() == 2u * 4u);
}
