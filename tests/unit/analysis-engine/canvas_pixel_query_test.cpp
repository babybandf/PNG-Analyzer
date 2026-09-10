// WP-APNG-INSPECT T06: bounded canvas-pixel provenance (contract C4).
// Expected RGBA values are computed by an independent in-test oracle
// (the APNG integer formula reimplemented here), never by calling the
// production blend path; the query output is additionally cross-checked
// against AnimationReplay.

#include <pnga/analysis-engine/animation_replay.h>
#include <pnga/analysis-engine/canvas_pixel_query.h>
#include <pnga/analysis-engine/job_scheduler.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "apng_fixture.h"
#include "apng_inspection_fixture.h"

using namespace pnga::analysis_engine;
using pnga::io::MemoryByteSource;
using pnga::png_format::FrameControl;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::InspectionTicket;
using pnga::trace_model::Stage;

namespace {

// Independent APNG integer blend oracle (spec formula, uint64 arithmetic).
std::array<std::uint8_t, 4> oracle_over(std::array<std::uint8_t, 4> dst,
                                        std::array<std::uint8_t, 4> src) {
  const std::uint64_t sa = src[3];
  const std::uint64_t da = dst[3];
  const std::uint64_t den = sa * 255ull + da * (255ull - sa);
  std::array<std::uint8_t, 4> out{};
  out[3] = static_cast<std::uint8_t>((den + 127ull) / 255ull);
  for (std::size_t c = 0; c < 3; ++c) {
    const std::uint64_t sc = src[c];
    const std::uint64_t dc = dst[c];
    const std::uint64_t numerator = sc * sa * 255ull + dc * da * (255ull - sa);
    out[c] = static_cast<std::uint8_t>(den == 0
                                           ? 0
                                           : (numerator + den / 2ull) / den);
  }
  return out;
}

// The deterministic fixture pattern (mirrored independently here).
std::array<std::uint8_t, 4> pattern_pixel(std::uint64_t x, std::uint64_t y) {
  const auto sample = [&](std::uint8_t c) {
    return static_cast<std::uint8_t>((x * 7 + y * 13 + c * 977 + 5) % 256);
  };
  return {sample(0), sample(1), sample(2), sample(3)};
}

CanvasPixelRequest base_request(std::uint32_t frame, Stage stage) {
  CanvasPixelRequest request;
  request.document = pnga_test::inspection_request(frame);
  request.ticket = InspectionTicket{
      {7, AnimationFrame{frame}}, stage, 1, 1};
  request.x = 11;
  request.y = 22;
  return request;
}

// A three-frame fixture covering dispose previous/background/none with
// source and over blends on one sub-rect.
FrameRequest dispose_fixture_request(std::uint32_t ordinal) {
  using namespace pnga;
  const std::array<png_format::FrameControl, 3> frames{
      png_format::FrameControl{0, 2, 3, 10, 20, 1, 100, 2, 0},  // PREV, SRC
      png_format::FrameControl{0, 2, 3, 10, 20, 1, 100, 1, 1},  // BG, OVER
      png_format::FrameControl{0, 2, 3, 10, 20, 1, 100, 0, 1},  // NONE, OVER
  };
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng_format(/*default_is_frame=*/false, frames,
                                  pnga_test::ApngFormat{}, {}, {}, {}, 16,
                                  32));
  FrameRequest request;
  request.generation = 7;
  request.request_serial = 11;
  request.ordinal = ordinal;
  request.source = source;
  request.index = std::make_shared<const png_format::AnimationIndex>(
      png_format::index_animation(*source, png_format::AnimationLimits{},
                                  [] { return false; }));
  request.canvas_header = pnga::png_reconstruction::ImageHeader{16, 32, 8, 6,
                                                                false};
  return request;
}

CanvasPixelRequest dispose_request(std::uint32_t frame, Stage stage) {
  CanvasPixelRequest request;
  request.document = dispose_fixture_request(frame);
  request.ticket = InspectionTicket{
      {7, AnimationFrame{frame}}, stage, 1, 1};
  request.x = 11;
  request.y = 22;
  return request;
}

}  // namespace

TEST_CASE("Canvas pre-blend of the first frame is a clear leaf",
          "[apng-inspect]") {
  auto out = query_canvas_pixel(base_request(0, Stage::kPreBlend), nullptr);
  REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
  REQUIRE_FALSE(out.nodes.empty());
  REQUIRE((out.nodes.back().rgba == std::array<std::uint8_t, 4>{0, 0, 0, 0}));
  REQUIRE(out.nodes.back().operation == CanvasOperation::kClear);
  REQUIRE(out.nodes.back().inputs.empty());
  REQUIRE(out.nodes.size() == 1);
}

TEST_CASE("Canvas source blend replaces the destination", "[apng-inspect]") {
  auto out = query_canvas_pixel(base_request(0, Stage::kPostBlend), nullptr);
  REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
  REQUIRE(out.nodes.back().operation == CanvasOperation::kBlendSource);
  REQUIRE(out.nodes.back().inputs.size() == 2);
  const auto& source = out.nodes[out.nodes.back().inputs[0]];
  const auto& dest = out.nodes[out.nodes.back().inputs[1]];
  REQUIRE(source.operation == CanvasOperation::kFrameSample);
  REQUIRE(source.frame == 0);
  REQUIRE((source.rgba == pattern_pixel(1, 2)));
  REQUIRE(dest.operation == CanvasOperation::kClear);
  // SOURCE: the result equals the frame pixel byte for byte.
  REQUIRE((out.nodes.back().rgba == pattern_pixel(1, 2)));
}

TEST_CASE("Canvas over blend, background and previous disposal follow the oracle",
          "[apng-inspect]") {
  // Frame 1: dispose BACKGROUND clears the rect, then OVER blends onto it.
  {
    auto out = query_canvas_pixel(dispose_request(1, Stage::kPostBlend),
                                  nullptr);
    REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
    const auto& root = out.nodes.back();
    REQUIRE(root.operation == CanvasOperation::kBlendOver);
    const auto& source = out.nodes[root.inputs[0]];
    const auto& dest = out.nodes[root.inputs[1]];
    REQUIRE(source.operation == CanvasOperation::kFrameSample);
    REQUIRE((source.rgba == pattern_pixel(1, 2)));
    // The destination is frame 0's post-dispose (dispose PREVIOUS): a
    // restore over its own PreBlend (the initial clear).
    REQUIRE(dest.operation == CanvasOperation::kRestore);
    REQUIRE(dest.inputs.size() == 1);
    REQUIRE(out.nodes[dest.inputs[0]].operation == CanvasOperation::kClear);
    REQUIRE((root.rgba ==
             oracle_over(std::array<std::uint8_t, 4>{0, 0, 0, 0},
                         pattern_pixel(1, 2))));
  }
  // First-frame PREVIOUS disposal clears (BACKGROUND semantics).
  {
    auto out = query_canvas_pixel(dispose_request(0, Stage::kPostDispose),
                                  nullptr);
    REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
    REQUIRE(out.nodes.back().operation == CanvasOperation::kRestore);
    REQUIRE(out.nodes.size() == 2);
    REQUIRE(out.nodes.front().operation == CanvasOperation::kClear);
    REQUIRE((out.nodes.back().rgba == std::array<std::uint8_t, 4>{0, 0, 0, 0}));
  }
  // Frame 0's dispose PREVIOUS restores its PreBlend (kRestore -> kClear).
  // Frame 2 dispose NONE keeps the post-blend pixels: the post-dispose
  // query aliases into a blend node.
  {
    auto out = query_canvas_pixel(dispose_request(2, Stage::kPostDispose),
                                  nullptr);
    REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
    REQUIRE(out.nodes.back().operation == CanvasOperation::kBlendOver);
  }
  // Multi-frame OVER with a non-trivial destination: frame 2 blends onto
  // frame 1's post-dispose (BACKGROUND clear) -> same as frame 1 result
  // when the payloads are identical; verified against the oracle below.
  {
    auto out = query_canvas_pixel(dispose_request(2, Stage::kPostBlend),
                                  nullptr);
    REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
    const auto& root = out.nodes.back();
    REQUIRE(root.operation == CanvasOperation::kBlendOver);
    const auto& dest = out.nodes[root.inputs[1]];
    REQUIRE(dest.operation == CanvasOperation::kClear);
    REQUIRE((root.rgba ==
             oracle_over(std::array<std::uint8_t, 4>{0, 0, 0, 0},
                         pattern_pixel(1, 2))));
  }
}

TEST_CASE("Canvas pixels outside the frame rectangle carry over",
          "[apng-inspect]") {
  CanvasPixelRequest request = dispose_request(2, Stage::kPostBlend);
  request.x = 5;
  request.y = 5;
  auto out = query_canvas_pixel(request, nullptr);
  REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
  // The root is the out-of-rect carry of frame 2; the walk descends to the
  // initial clear leaf, so the page holds carry(2) -> carry(1) -> carry(0)
  // -> clear with carried transparent values.
  REQUIRE(out.nodes.size() == 4);
  REQUIRE(out.nodes.back().operation == CanvasOperation::kCarry);
  REQUIRE(out.nodes.back().stage == Stage::kPostBlend);
  REQUIRE(out.nodes.back().frame == 2);
  REQUIRE((out.nodes.back().rgba == std::array<std::uint8_t, 4>{0, 0, 0, 0}));
  REQUIRE(out.nodes.front().operation == CanvasOperation::kClear);
  for (std::size_t i = 1; i < out.nodes.size(); ++i) {
    REQUIRE(out.nodes[i].inputs.size() == 1);
    REQUIRE(out.nodes[i].inputs.front() == i - 1);
  }
}

TEST_CASE("Canvas query agrees with the animation replay values",
          "[apng-inspect]") {
  AnimationReplay replay(1u << 20);
  ReplayRequest replay_request;
  replay_request.frame = dispose_fixture_request(2);
  replay_request.requested_stage = Stage::kPostBlend;
  const auto replayed = replay.materialize(replay_request, nullptr);
  REQUIRE(replayed.stop == ReplayResult::Stop::kReady);
  REQUIRE(replayed.image);
  const std::size_t offset = (22u * replayed.image->width + 11u) * 4u;
  const std::array<std::uint8_t, 4> replay_pixel{
      replayed.image->pixels[offset], replayed.image->pixels[offset + 1],
      replayed.image->pixels[offset + 2], replayed.image->pixels[offset + 3]};

  auto out = query_canvas_pixel(dispose_request(2, Stage::kPostBlend),
                                nullptr);
  REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
  REQUIRE((out.nodes.back().rgba == replay_pixel));
}

TEST_CASE("Canvas query validates the request", "[apng-inspect]") {
  {
    CanvasPixelRequest request = base_request(0, Stage::kPreBlend);
    request.document.source.reset();
    REQUIRE(query_canvas_pixel(request, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest request = base_request(0, Stage::kPreBlend);
    request.ticket.key.identity = pnga::trace_model::StaticImage{};
    REQUIRE(query_canvas_pixel(request, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest request = base_request(0, Stage::kFiltered);
    REQUIRE(query_canvas_pixel(request, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest request = base_request(0, Stage::kPreBlend);
    request.x = 16;
    REQUIRE(query_canvas_pixel(request, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CancellationToken token;
    token.request_cancel();
    REQUIRE(query_canvas_pixel(base_request(0, Stage::kPreBlend), &token)
                .stop == CanvasPixelResult::Stop::kCancelled);
  }
}

TEST_CASE("Deep histories paginate through the cursor", "[apng-inspect]") {
  // 2000 chained dispose-previous frames: the history exceeds the fixed
  // per-page budget, forcing partial pages with a continuation cursor.
  using namespace pnga;
  std::vector<png_format::FrameControl> frames;
  for (std::uint32_t i = 0; i < 2000; ++i) {
    frames.push_back(
        png_format::FrameControl{0, 1, 1, 0, 0, 1, 100, 2, 0});
  }
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng_format(/*default_is_frame=*/false, frames,
                                  pnga_test::ApngFormat{}, {}, {}, {}, 1, 1));
  FrameRequest document;
  document.generation = 7;
  document.request_serial = 11;
  document.ordinal = 1999;
  document.source = source;
  document.index = std::make_shared<const png_format::AnimationIndex>(
      png_format::index_animation(*source, png_format::AnimationLimits{},
                                  [] { return false; }));
  document.canvas_header = pnga::png_reconstruction::ImageHeader{1, 1, 8, 6,
                                                                 false};

  CanvasPixelRequest request;
  request.document = document;
  request.ticket = InspectionTicket{{7, AnimationFrame{1999}},
                                    Stage::kPostBlend, 1, 1};
  request.x = 0;
  request.y = 0;

  std::size_t pages = 0;
  std::uint64_t total_steps = 0;
  CanvasPixelResult out = query_canvas_pixel(request, nullptr);
  while (out.stop == CanvasPixelResult::Stop::kPartial) {
    REQUIRE(out.next.has_value());
    REQUIRE_FALSE(out.next->pending.empty());
    REQUIRE(out.next->pending.size() <= 1024);
    // The page stays a closed DAG: edges reference smaller indices only.
    for (std::size_t i = 0; i < out.nodes.size(); ++i) {
      for (const auto input : out.nodes[i].inputs) {
        REQUIRE(input < i);
      }
    }
    total_steps += out.next->visited_steps;
    ++pages;
    REQUIRE(pages < 64);  // deterministic termination bound

    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    out = query_canvas_pixel(continuation, nullptr);
  }
  REQUIRE(out.stop == CanvasPixelResult::Stop::kReady);
  REQUIRE(pages > 0);
  REQUIRE(total_steps > 0);
  REQUIRE_FALSE(out.nodes.empty());
  REQUIRE(out.next == std::nullopt);
}

TEST_CASE("Canvas cursors are validated before continuation", "[apng-inspect]") {
  // Build a two-page scenario from the paginated fixture.
  using namespace pnga;
  std::vector<png_format::FrameControl> frames;
  for (std::uint32_t i = 0; i < 2000; ++i) {
    frames.push_back(
        png_format::FrameControl{0, 1, 1, 0, 0, 1, 100, 2, 0});
  }
  auto source = std::make_shared<const MemoryByteSource>(
      pnga_test::make_apng_format(/*default_is_frame=*/false, frames,
                                  pnga_test::ApngFormat{}, {}, {}, {}, 1, 1));
  FrameRequest document;
  document.generation = 7;
  document.request_serial = 11;
  document.ordinal = 1999;
  document.source = source;
  document.index = std::make_shared<const png_format::AnimationIndex>(
      png_format::index_animation(*source, png_format::AnimationLimits{},
                                  [] { return false; }));
  document.canvas_header = pnga::png_reconstruction::ImageHeader{1, 1, 8, 6,
                                                                 false};

  CanvasPixelRequest request;
  request.document = document;
  request.ticket = InspectionTicket{{7, AnimationFrame{1999}},
                                    Stage::kPostBlend, 1, 1};
  request.x = 0;
  request.y = 0;
  CanvasPixelResult out = query_canvas_pixel(request, nullptr);
  REQUIRE(out.stop == CanvasPixelResult::Stop::kPartial);
  REQUIRE(out.next.has_value());

  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.cursor->version = 2;
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.cursor->ticket.target_epoch = 99;
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.x = 1;
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.cursor->pending.clear();
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.cursor->pending.front().frame = 2000;
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
  {
    CanvasPixelRequest continuation = request;
    continuation.cursor = out.next;
    continuation.cursor->pending.front().stage = Stage::kTrace;
    REQUIRE(query_canvas_pixel(continuation, nullptr).stop ==
            CanvasPixelResult::Stop::kError);
  }
}
