// WP-604A: fixed, generated performance corpus and measurement runner.
// Threshold enforcement belongs to WP-604B; this executable only checks that
// each scenario completes successfully and emits a stable record shape.
// WP-607C: the large scenario consumes the shared perf-large-rgba8 corpus
// fixture and the record carries the aggregate corpus revision from the
// compile-time definition.
// WP-5U12F: the compression-inspector scenario gates the bounded Compression
// inspector pipeline over the WP-607C corpus (Fast Index projection, bounded
// 4,096-token Deep Trace query, three inspector model publications, first
// visible rows, 200 deterministic row reads and a checksum). Screenshot
// capture is deliberately not measured here; visual evidence belongs to the
// GUI product gate.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/analysis-engine/pixel_provenance.h>
#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/trace_query.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "controlled_fixture.h"
#include "test_png_helpers.h"

#ifndef PNGA_WP607C_CORPUS_REVISION
#error "PNGA_WP607C_CORPUS_REVISION must be defined by the build"
#endif

namespace {

using Clock = std::chrono::steady_clock;
using pnga::analysis_engine::PixelProvenanceResult;
using pnga::analysis_engine::ScanlineAnchorIndexResult;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::VirtualIDATStream;
using pnga::png_reconstruction::ImageHeader;
using pnga_test::wp607c::ControlledCaseId;
using pnga_test::wp607c::ControlledFixture;

// WP-5U12F compression-inspector scenario bounds. The replay budgets mirror
// the production bounded trace path (WP-5U13 trace_controller /
// trace_orchestrator) so the measured pipeline is the published behavior.
constexpr std::uint64_t kInspectorMaxOutputBytes = 1ull << 26;   // 64 MiB index budget
constexpr std::uint64_t kBoundedTraceTokens = 4096;              // kMaxTraceTokens
constexpr std::uint64_t kTraceLookaheadBytes = 64ull * 1024ull;  // replay look-ahead
constexpr std::uint64_t kQueryWindowBytes = 65536;               // Deep Trace query window
constexpr std::uint64_t kSmallCaseOutputBytes = 1ull << 20;      // corpus trace budget
constexpr std::uint64_t kFirstVisibleRows = 32;
constexpr std::uint64_t kVisibleRowReads = 200;
// RFC 1951 section 3.2.6 fixed literal/length cardinality: the maximum
// bounded Huffman table the projection can publish.
constexpr std::size_t kMaxHuffmanTableEntries = 288;

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
  ControlledFixture fixture;
  ImageHeader header{};
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
      : fixture(pnga_test::wp607c::make_controlled_fixture(
              pnga_test::wp607c::ControlledCaseId::kPerfLargeRgba8)),
        source(std::make_shared<MemoryByteSource>(fixture.png_bytes)),
        chunks(pnga::png_format::index_chunks(*source)),
        stream(std::make_unique<VirtualIDATStream>(chunks)) {
    // The production header derives from the fixture's independent facts.
    const auto& facts = fixture.expected.image;
    require(facts.has_value(), "performance corpus: missing image facts");
    require(facts->bit_depth == 8 && facts->color_type == 6 &&
                facts->interlace == 0,
            "performance corpus: unexpected perf-large-rgba8 facts");
    header = ImageHeader{facts->width, facts->height, facts->bit_depth,
                         facts->color_type, facts->interlace != 0};
    require(chunks.valid_signature,
            "performance corpus: invalid PNG signature");
    const auto index_time = timed([&] {
      chunks = pnga::png_format::index_chunks(*source);
    });
    chunk_index_us = index_time.micros;
    stream = std::make_unique<VirtualIDATStream>(chunks);

    const auto fast_time = timed([&] {
      anchors = pnga::analysis_engine::build_scanline_anchors(
          *stream, *source, header, 64u * 1024u, 16u * 1024u * 1024u);
    });
    fast_index_us = fast_time.micros;
    require(anchors.success, "performance corpus: fast index failed");
    require(anchors.scanline_count == header.height,
            "performance corpus: unexpected scanline count");

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
  pnga_test::EncodedPng image;
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

// Adapts a VirtualIDATStream to IByteSource (the production DEFLATE modules
// consume a generic byte stream and never assume IDAT data is contiguous).
class VirtualIdatSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatSource(const VirtualIDATStream& stream,
                    const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

// One corpus case with its logical zlib stream. The DEFLATE block index runs
// inside the measured region that owns it (index_blocks_now), so the Fast
// Index projection metric covers the complete Blocks publication work.
struct InspectorCase {
  ControlledFixture fixture;
  std::shared_ptr<MemoryByteSource> source;
  ChunkIndex chunks;
  std::unique_ptr<VirtualIDATStream> stream;
  std::unique_ptr<VirtualIdatSource> logical;
  pnga::deflate_index::BlockIndexResult blocks;

  explicit InspectorCase(ControlledFixture input)
      : fixture(std::move(input)),
        source(std::make_shared<MemoryByteSource>(fixture.png_bytes)),
        chunks(pnga::png_format::index_chunks(*source)),
        stream(std::make_unique<VirtualIDATStream>(chunks)),
        logical(std::make_unique<VirtualIdatSource>(*stream, *source)) {
    require(chunks.valid_signature,
            "compression inspector: invalid PNG signature");
  }

  void index_blocks_now() {
    blocks = pnga::deflate_index::index_blocks(*logical,
                                               kInspectorMaxOutputBytes);
    require(blocks.success, "compression inspector: block index failed");
  }
};

struct CompressionInspectorMetrics {
  std::uint64_t png_bytes = 0;
  std::uint64_t block_count = 0;
  std::uint64_t fast_index_us = 0;
  std::uint64_t trace_query_4096_us = 0;
  std::uint64_t huffman_model_us = 0;
  std::uint64_t decode_trace_model_us = 0;
  std::uint64_t first_visible_rows_us = 0;
  std::uint64_t visible_row_reads_us = 0;
  std::uint64_t checksum = 0;
};

// Deterministic visible-row target, the fixed sequence convention of the
// other scenarios (no clock, no randomness).
std::uint64_t visible_row_target(std::uint64_t i, std::uint64_t rows) {
  return (i * 2654435761ull + 17ull) % rows;
}

CompressionInspectorMetrics run_compression_inspector_scenario() {
  CompressionInspectorMetrics metrics;

  // --- Fast Index projection (perf-large-rgba8) ------------------------------
  // The complete Blocks model publication work on open: DEFLATE block index
  // plus the generation-level Fast Compression Index view.
  InspectorCase large(
      make_controlled_fixture(ControlledCaseId::kPerfLargeRgba8));
  const auto& large_facts = large.fixture.expected;
  require(large_facts.image.has_value(),
          "compression inspector: missing large image facts");
  metrics.png_bytes = large.fixture.png_bytes.size();
  metrics.block_count = large_facts.blocks.size();
  pnga::analysis_engine::FastCompressionIndexView fast_index;
  const auto fast_time = timed([&] {
    large.index_blocks_now();
    fast_index = pnga::analysis_engine::build_fast_compression_index(
        1, large.blocks, *large.stream);
  });
  metrics.fast_index_us = fast_time.micros;
  require(fast_index.status ==
              pnga::analysis_engine::FastCompressionIndexStatus::kReady,
          "compression inspector: fast index is not ready");
  require(fast_index.blocks.size() == metrics.block_count,
          "compression inspector: fast index row count differs from "
          "corpus block facts");

  // --- bounded 4,096-token Deep Trace query (perf-large-rgba8) ---------------
  // One stored block window plus the production look-ahead; the token budget
  // stops the published result at exactly kBoundedTraceTokens rows.
  pnga::trace_model::Selection selection;
  selection.stage = pnga::trace_model::Stage::kDelivered;
  pnga::analysis_engine::TraceQueryResult query;
  const auto query_time = timed([&] {
    const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
        *large.logical, kQueryWindowBytes + kTraceLookaheadBytes);
    query = pnga::analysis_engine::compose_trace_query(
        1, selection, large.blocks, trace, *large.stream, *large.source, 0,
        kQueryWindowBytes, kBoundedTraceTokens);
  });
  metrics.trace_query_4096_us = query_time.micros;
  require(query.status == pnga::analysis_engine::TraceQueryStatus::kPartial,
          "compression inspector: bounded query must stop at the budget");
  require(query.truncated,
          "compression inspector: bounded query must report truncation");
  require(query.tokens.size() == kBoundedTraceTokens,
          "compression inspector: bounded query must return exactly 4096 "
          "tokens");

  // --- Huffman model publications (fixed + dynamic corpus blocks) ------------
  InspectorCase fixed_case(
      make_controlled_fixture(ControlledCaseId::kTraceFixedNonoverlap));
  InspectorCase dynamic_case(
      make_controlled_fixture(ControlledCaseId::kTraceDynamicOverlapRepeats));
  fixed_case.index_blocks_now();
  dynamic_case.index_blocks_now();
  pnga::analysis_engine::TraceQueryResult fixed_query;
  pnga::analysis_engine::TraceQueryResult dynamic_query;
  {
    const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
        *fixed_case.logical, kSmallCaseOutputBytes);
    require(trace.success, "compression inspector: fixed replay failed");
    fixed_query = pnga::analysis_engine::compose_trace_query(
        1, selection, fixed_case.blocks, trace, *fixed_case.stream,
        *fixed_case.source, 0, fixed_case.blocks.total_output_bytes,
        kBoundedTraceTokens);
    require(fixed_query.status ==
                pnga::analysis_engine::TraceQueryStatus::kReady,
            "compression inspector: fixed bounded query is not ready");
  }
  {
    const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
        *dynamic_case.logical, kSmallCaseOutputBytes);
    require(trace.success, "compression inspector: dynamic replay failed");
    dynamic_query = pnga::analysis_engine::compose_trace_query(
        1, selection, dynamic_case.blocks, trace, *dynamic_case.stream,
        *dynamic_case.source, 0, dynamic_case.blocks.total_output_bytes,
        kBoundedTraceTokens);
    require(dynamic_query.status ==
                pnga::analysis_engine::TraceQueryStatus::kReady,
            "compression inspector: dynamic bounded query is not ready");
  }
  pnga::analysis_engine::HuffmanInspectorView fixed_huffman;
  pnga::analysis_engine::HuffmanInspectorView dynamic_huffman;
  const auto huffman_time = timed([&] {
    fixed_huffman = pnga::analysis_engine::build_huffman_inspector(
        fixed_query);
    dynamic_huffman = pnga::analysis_engine::build_huffman_inspector(
        dynamic_query);
  });
  metrics.huffman_model_us = huffman_time.micros;
  require(!fixed_huffman.tables.empty() &&
              fixed_huffman.tables[0].entries.size() ==
                  kMaxHuffmanTableEntries &&
              fixed_huffman.tables[0].declared_entry_count ==
                  kMaxHuffmanTableEntries,
          "compression inspector: the maximum bounded Huffman table must "
          "be complete");
  require(!dynamic_huffman.tables.empty() &&
              !dynamic_huffman.tables[0].entries.empty(),
          "compression inspector: dynamic Huffman table is empty");

  // --- Decode Trace model publications ---------------------------------------
  pnga::analysis_engine::DecodeTraceInspectorView large_decode;
  pnga::analysis_engine::DecodeTraceInspectorView dynamic_decode;
  const auto decode_model_time = timed([&] {
    large_decode =
        pnga::analysis_engine::build_decode_trace_inspector(query);
    dynamic_decode =
        pnga::analysis_engine::build_decode_trace_inspector(dynamic_query);
  });
  metrics.decode_trace_model_us = decode_model_time.micros;
  require(large_decode.scope.returned_token_count == kBoundedTraceTokens &&
              large_decode.scope.truncated &&
              large_decode.steps.size() == kBoundedTraceTokens,
          "compression inspector: decode trace publication is not the "
          "bounded 4096-token window");
  for (std::uint64_t i = 0; i < kBoundedTraceTokens; ++i) {
    require(large_decode.steps[i].token_index == i &&
                large_decode.steps[i].output_range.begin.value == i &&
                large_decode.steps[i].output_range.end.value == i + 1,
            "compression inspector: decode trace output ranges do not "
            "tile the bounded window");
  }
  require(!dynamic_decode.steps.empty(),
          "compression inspector: dynamic decode trace is empty");

  // --- first visible rows ------------------------------------------------------
  // Deterministic on-demand formatting of the first visible rows of the
  // three published models.
  const auto first_rows_time = timed([&] {
    const std::uint64_t block_rows =
        std::min<std::uint64_t>(kFirstVisibleRows, fast_index.blocks.size());
    for (std::uint64_t i = 0; i < block_rows; ++i) {
      const auto& row = fast_index.blocks[i];
      require(row.output_range.begin.value ==
                      large_facts.blocks[i].output_bytes.begin &&
                  row.output_range.end.value ==
                      large_facts.blocks[i].output_bytes.end,
              "compression inspector: block row differs from corpus fact");
      const std::string text = std::to_string(row.block_index) + " " +
                               std::to_string(row.output_range.begin.value);
      metrics.checksum += text.size();
    }
    const auto& table = fixed_huffman.tables[0];
    const std::uint64_t entry_rows =
        std::min<std::uint64_t>(kFirstVisibleRows, table.entries.size());
    for (std::uint64_t i = 0; i < entry_rows; ++i) {
      const auto& entry = table.entries[i];
      const std::string text = std::to_string(entry.symbol) + " " +
                               std::to_string(entry.bit_length) + " " +
                               entry.canonical_bits;
      metrics.checksum += text.size();
    }
    const std::uint64_t step_rows = std::min<std::uint64_t>(
        kFirstVisibleRows, large_decode.steps.size());
    for (std::uint64_t i = 0; i < step_rows; ++i) {
      const auto& step = large_decode.steps[i];
      metrics.checksum += step.event_text.size();
    }
  });
  metrics.first_visible_rows_us = first_rows_time.micros;

  // --- 200 deterministic visible-row reads ------------------------------------
  const auto reads_time = timed([&] {
    for (std::uint64_t i = 0; i < kVisibleRowReads; ++i) {
      const auto& block = fast_index.blocks[visible_row_target(
          i, static_cast<std::uint64_t>(fast_index.blocks.size()))];
      metrics.checksum += block.output_range.end.value -
                          block.output_range.begin.value;
      const auto& entry = fixed_huffman.tables[0].entries[visible_row_target(
          i, fixed_huffman.tables[0].entries.size())];
      metrics.checksum += entry.bit_length;
      const auto& step = large_decode.steps[visible_row_target(
          i, static_cast<std::uint64_t>(large_decode.steps.size()))];
      metrics.checksum += step.output_range.begin.value;
    }
  });
  metrics.visible_row_reads_us = reads_time.micros;
  return metrics;
}

void emit_record(const LargeScenario& large,
                 const ProvenanceScenario& provenance,
                 const CompressionInspectorMetrics& inspector) {
  constexpr const char* kCorpusRevision = PNGA_WP607C_CORPUS_REVISION;
  require(std::strlen(kCorpusRevision) == 64,
          "performance corpus revision must be 64 hex characters");
  std::cout << "{\"schema\":\"pnga-performance-v1\","
                "\"corpus\":\"wp607c-static-v1\","
                "\"corpus_revision\":\"" << kCorpusRevision << "\","
                "\"large_case\":\"perf-large-rgba8\","
                "\"scenarios\":["
                "{\"id\":\"large-index\",\"width\":1024,\"height\":768,"
                "\"bit_depth\":8,\"color_type\":6,\"interlace\":0,"
                "\"png_bytes\":"
             << large.fixture.png_bytes.size()
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
            << ",\"checksum\":" << provenance.checksum << "},"
                "{\"id\":\"compression-inspector\",\"width\":1024,"
                "\"height\":768,\"bit_depth\":8,\"color_type\":6,"
                "\"interlace\":0,\"png_bytes\":" << inspector.png_bytes
            << ",\"block_count\":" << inspector.block_count
            << ",\"fast_index_us\":" << inspector.fast_index_us
            << ",\"trace_query_4096_us\":" << inspector.trace_query_4096_us
            << ",\"huffman_model_us\":" << inspector.huffman_model_us
            << ",\"decode_trace_model_us\":"
            << inspector.decode_trace_model_us
            << ",\"first_visible_rows_us\":"
            << inspector.first_visible_rows_us
            << ",\"visible_row_reads_us\":"
            << inspector.visible_row_reads_us
            << ",\"checksum\":" << inspector.checksum << "}],"
                "\"ui_scenario\":\"gui_trace_inspector_performance_tests\"}\n";
}

}  // namespace

int main() {
  try {
    const LargeScenario large;
    const ProvenanceScenario provenance;
    const CompressionInspectorMetrics inspector =
        run_compression_inspector_scenario();
    emit_record(large, provenance, inspector);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "performance runner: FAIL: " << error.what() << '\n';
    return 1;
  }
}
