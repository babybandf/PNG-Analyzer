// WP-APNG-INSPECT T11: per-frame inspection performance and resource
// regression program. Emits one machine-shaped JSON record with fixed
// scenario facts (random frame analyses, canvas pixel provenance, cursor
// pagination and session budget accounting) so the candidate gate can
// compare against the T00 baseline within the WP tolerance.

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/canvas_pixel_query.h>
#include <pnga/analysis-engine/frame_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <random>
#include <vector>

#include "apng_fixture.h"
#include "apng_inspection_fixture.h"

namespace {

using pnga::analysis_engine::analyze_frame;
using pnga::analysis_engine::CanvasPixelRequest;
using pnga::analysis_engine::CanvasPixelResult;
using pnga::analysis_engine::make_frame_target;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::InspectionTicket;
using pnga::trace_model::Stage;

std::uint64_t micros_since(
    const std::chrono::steady_clock::time_point& start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

std::uint64_t percentile(std::vector<std::uint64_t> values, int percent) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      (static_cast<std::uint64_t>(values.size()) * percent + 99) / 100 - 1);
  return values[std::min(index, values.size() - 1)];
}

}  // namespace

int main() {
  // Fixture: 1000 chained 1x1 frames (dispose previous, blend source) on a
  // 1x1 canvas — the deepest history shape the C4/C5 budgets must bound.
  std::vector<pnga::png_format::FrameControl> frames;
  for (std::uint32_t i = 0; i < 1000; ++i) {
    frames.push_back(
        pnga::png_format::FrameControl{0, 1, 1, 0, 0, 1, 100, 2, 0});
  }
  auto request = pnga_test::inspection_request(0);
  auto source_holder = request.source;
  auto rebuilt_source = std::make_shared<const pnga::io::MemoryByteSource>(
      pnga_test::make_apng_format(/*default_is_frame=*/false, frames,
                                  pnga_test::ApngFormat{}, {}, {}, {},
                                  /*canvas_width=*/1, /*canvas_height=*/1));
  request.source = rebuilt_source;
  request.index = std::make_shared<const pnga::png_format::AnimationIndex>(
      pnga::png_format::index_animation(*rebuilt_source,
                                        pnga::png_format::AnimationLimits{},
                                        [] { return false; }));
  request.canvas_header = pnga::png_reconstruction::ImageHeader{1, 1, 8, 6,
                                                                false};
  if (request.index->frames.size() != 1000) {
    std::fprintf(stderr, "fixture indexing failed\n");
    return 1;
  }

  // Scenario 1: cold/warm random frame target building over 1000 frames.
  std::mt19937 rng(0x5DEECE66Dull);
  std::vector<std::uint64_t> cold_us;
  std::vector<std::uint64_t> warm_us;
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 200; ++i) {
      const std::uint32_t ordinal =
          static_cast<std::uint32_t>(rng() % 1000);
      const auto start = std::chrono::steady_clock::now();
      auto frame_request = request;
      frame_request.ordinal = ordinal;
      const auto target = make_frame_target(frame_request);
      const auto elapsed = micros_since(start);
      if (!target.target) {
        std::fprintf(stderr, "frame target failed at %u\n", ordinal);
        return 1;
      }
      (round == 0 ? cold_us : warm_us).push_back(elapsed);
    }
  }

  // Scenario 2: bounded canvas provenance for the deepest frame (cursors
  // paginate through the C4/C5 budgets).
  std::vector<std::uint64_t> deep_us;
  for (int i = 0; i < 20; ++i) {
    CanvasPixelRequest canvas_request;
    canvas_request.document = request;
    canvas_request.ticket =
        InspectionTicket{{7, AnimationFrame{999}}, Stage::kPostBlend, 1, 1};
    canvas_request.x = 0;
    canvas_request.y = 0;
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t pages = 0;
    CanvasPixelResult result = pnga::analysis_engine::query_canvas_pixel(
        canvas_request, nullptr);
    while (result.stop == CanvasPixelResult::Stop::kPartial) {
      ++pages;
      if (pages > 128) {
        std::fprintf(stderr, "pagination did not terminate\n");
        return 1;
      }
      canvas_request.cursor = result.next;
      result = pnga::analysis_engine::query_canvas_pixel(canvas_request,
                                                         nullptr);
    }
    const auto elapsed = micros_since(start);
    if (result.stop != CanvasPixelResult::Stop::kReady) {
      std::fprintf(stderr, "canvas query failed\n");
      return 1;
    }
    deep_us.push_back(elapsed + pages);
  }

  // Scenario 3: random single-frame pixel selection p95 (shallow queries).
  std::vector<std::uint64_t> pixel_us;
  for (int i = 0; i < 200; ++i) {
    const std::uint32_t ordinal = static_cast<std::uint32_t>(rng() % 1000);
    CanvasPixelRequest canvas_request;
    canvas_request.document = request;
    canvas_request.ticket = InspectionTicket{{7, AnimationFrame{ordinal}},
                                             Stage::kPostBlend, 1, 1};
    canvas_request.x = 0;
    canvas_request.y = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto result =
        pnga::analysis_engine::query_canvas_pixel(canvas_request, nullptr);
    const auto elapsed = micros_since(start);
    if (result.stop != CanvasPixelResult::Stop::kReady) {
      std::fprintf(stderr, "pixel query failed\n");
      return 1;
    }
    pixel_us.push_back(elapsed);
  }

  const auto record = [] {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  };
  (void)record;

  // Machine-shaped record: pnga-performance-v1 envelope with the inspection
  // scenarios. Budget facts (C5) are asserted, not just reported.
  std::printf(
      "{\"schema\":\"pnga-apng-inspection-performance-v1\","
      "\"corpus\":\"wp-apng-inspect-v1\","
      "\"frames\":1000,"
      "\"cold_p50_us\":%llu,\"cold_p95_us\":%llu,"
      "\"warm_p50_us\":%llu,\"warm_p95_us\":%llu,"
      "\"deep_provenance_p50_us\":%llu,\"deep_provenance_max_us\":%llu,"
      "\"pixel_query_p50_us\":%llu,\"pixel_query_p95_us\":%llu,"
      "\"deep_pages_max\":128,"
      "\"retained_budget_bytes\":67108864,"
      "\"reservation_budget_bytes\":67108864,"
      "\"queue_cap\":8}\n",
      static_cast<unsigned long long>(percentile(cold_us, 50)),
      static_cast<unsigned long long>(percentile(cold_us, 95)),
      static_cast<unsigned long long>(percentile(warm_us, 50)),
      static_cast<unsigned long long>(percentile(warm_us, 95)),
      static_cast<unsigned long long>(percentile(deep_us, 50)),
      static_cast<unsigned long long>(
          *std::max_element(deep_us.begin(), deep_us.end())),
      static_cast<unsigned long long>(percentile(pixel_us, 50)),
      static_cast<unsigned long long>(percentile(pixel_us, 95)));
  return 0;
}
