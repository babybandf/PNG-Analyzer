// WP-604A: fixed, generated performance corpus and measurement runner.
// Threshold enforcement belongs to WP-604B; this executable only checks that
// each scenario completes successfully and emits a stable record shape.

#include <pnga/analysis-engine/pixel_provenance.h>
#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_png_helpers.h"

namespace {

using Clock = std::chrono::steady_clock;
using pnga::analysis_engine::PixelProvenanceResult;
using pnga::analysis_engine::ScanlineAnchorIndexResult;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using pnga_test::EncodedPng;

struct TimedValue {
  std::uint64_t micros = 0;
};

template <typename Function>
TimedValue timed(Function&& function) {
  const auto start = Clock::now();
  function();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
  return TimedValue{static_cast<std::uint64_t>(elapsed.count())};
}

std::uint64_t percentile(std::vector<std::uint64_t> values,
                         std::uint64_t numerator) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::uint64_t rank =
      (static_cast<std::uint64_t>(values.size()) * numerator + 99) / 100;
  const std::size_t index = static_cast<std::size_t>(
      std::min<std::uint64_t>(rank == 0 ? 0 : rank - 1, values.size() - 1));
  return values[index];
}

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct LargeScenario {
  EncodedPng image;
  std::shared_ptr<MemoryByteSource> source;
  ChunkIndex chunks;
  std::unique_ptr<VirtualIDATStream> stream;
  ScanlineAnchorIndexResult anchors;
  std::uint64_t chunk_index_us = 0;
  std::uint64_t fast_index_us = 0;
  std::uint64_t reopen_index_us = 0;
  std::uint64_t row_p50_us = 0;
  std::uint64_t row_p95_us = 0;
  std::uint64_t checksum = 0;

  LargeScenario()
      : image(pnga_test::encode_png(1024, 768, 8, 6, false, true, 604)),
        source(std::make_shared<MemoryByteSource>(image.png_bytes)),
        chunks(pnga::png_format::index_chunks(*source)),
        stream(std::make_unique<VirtualIDATStream>(chunks)) {
    require(chunks.valid_signature, "performance corpus: invalid PNG signature");
    const auto index_time = timed([&] {
      chunks = pnga::png_format::index_chunks(*source);
    });
    chunk_index_us = index_time.micros;
    stream = std::make_unique<VirtualIDATStream>(chunks);

    const auto fast_time = timed([&] {
      anchors = pnga::analysis_engine::build_scanline_anchors(
          *stream, *source, image.header, 64u * 1024u, 16u * 1024u * 1024u);
    });
    fast_index_us = fast_time.micros;
    require(anchors.success, "performance corpus: fast index failed");

    const auto reopen_time = timed([&] {
      const ChunkIndex reopened = pnga::png_format::index_chunks(*source);
      require(reopened.valid_signature, "performance corpus: reopen failed");
    });
    reopen_index_us = reopen_time.micros;

    std::vector<std::uint64_t> row_times;
    row_times.reserve(64);
    for (std::uint64_t i = 0; i < 64; ++i) {
      const std::uint64_t row =
          (i * 2654435761ull + 17ull) % anchors.scanline_count;
      TimedValue restore_time = timed([&] {
        const auto restored = pnga::analysis_engine::restore_scanline(
            anchors, *stream, *source, row);
        require(restored.success, "performance corpus: random row failed");
        checksum += restored.unfiltered.size();
      });
      row_times.push_back(restore_time.micros);
    }
    row_p50_us = percentile(row_times, 50);
    row_p95_us = percentile(row_times, 95);
  }
};

struct ProvenanceScenario {
  EncodedPng image;
  std::shared_ptr<MemoryByteSource> source;
  ChunkIndex chunks;
  std::unique_ptr<VirtualIDATStream> stream;
  pnga::analysis_engine::StageSet stages;
  std::uint64_t preview_us = 0;
  std::uint64_t provenance_p50_us = 0;
  std::uint64_t provenance_p95_us = 0;
  std::uint64_t checksum = 0;

  ProvenanceScenario()
      : image(pnga_test::encode_png(8, 5, 8, 6, false, false, 604)),
        source(std::make_shared<MemoryByteSource>(image.png_bytes)),
        chunks(pnga::png_format::index_chunks(*source)),
        stream(std::make_unique<VirtualIDATStream>(chunks)) {
    require(chunks.valid_signature, "performance provenance: invalid PNG");
    const auto preview_time = timed([&] {
      stages = pnga::analysis_engine::analyze_stages(*stream, *source,
                                                     image.header);
    });
    preview_us = preview_time.micros;
    require(stages.success, "performance provenance: stage analysis failed");

    std::vector<std::uint64_t> provenance_times;
    provenance_times.reserve(16);
    for (std::uint64_t i = 0; i < 16; ++i) {
      const std::uint64_t x = 3;
      const std::uint64_t y = 2;
      TimedValue query_time = timed([&] {
        const PixelProvenanceResult result =
            pnga::analysis_engine::query_pixel_provenance(
                stages, *stream, *source, x, y, i % 4, 1u << 20);
        if (!result.success) {
          throw std::runtime_error("performance provenance: " + result.error);
        }
        checksum += result.physical_input.size() + result.token_output_ranges.size();
      });
      provenance_times.push_back(query_time.micros);
    }
    provenance_p50_us = percentile(provenance_times, 50);
    provenance_p95_us = percentile(provenance_times, 95);
  }
};

void emit_record(const LargeScenario& large,
                 const ProvenanceScenario& provenance) {
  std::cout << "{\"schema\":\"pnga-performance-v1\","
               "\"corpus\":\"generated-static-png-v1\","
               "\"scenarios\":["
               "{\"id\":\"large-index\",\"width\":1024,\"height\":768,"
               "\"bit_depth\":8,\"color_type\":6,\"interlace\":0,"
               "\"png_bytes\":"
            << large.image.png_bytes.size()
            << ",\"chunk_index_us\":" << large.chunk_index_us
            << ",\"fast_index_us\":" << large.fast_index_us
            << ",\"reopen_index_us\":" << large.reopen_index_us
            << ",\"random_row_count\":64,\"random_row_p50_us\":"
            << large.row_p50_us << ",\"random_row_p95_us\":" << large.row_p95_us
            << ",\"checksum\":" << large.checksum << "},"
               "{\"id\":\"pixel-provenance\",\"width\":8,\"height\":5,"
               "\"bit_depth\":8,\"color_type\":6,\"interlace\":0,"
               "\"png_bytes\":"
            << provenance.image.png_bytes.size()
            << ",\"preview_us\":" << provenance.preview_us
            << ",\"pixel_query_count\":16,\"pixel_query_p50_us\":"
            << provenance.provenance_p50_us
            << ",\"pixel_query_p95_us\":" << provenance.provenance_p95_us
            << ",\"checksum\":" << provenance.checksum << "}],"
               "\"ui_scenario\":\"gui_trace_inspector_performance_tests\"}\n";
}

}  // namespace

int main() {
  try {
    const LargeScenario large;
    const ProvenanceScenario provenance;
    emit_record(large, provenance);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "performance runner: FAIL: " << error.what() << '\n';
    return 1;
  }
}
