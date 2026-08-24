// WP-5T0A trace query contract tests: bounded composition, stable statuses,
// logical/physical bit provenance, deterministic serialization and partial
// results when either the fast index or the replay is unavailable.

#include <pnga/analysis-engine/trace_query.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_png_helpers.h"

using pnga::analysis_engine::TraceQueryStatus;
using pnga::analysis_engine::compose_trace_query;
using pnga::analysis_engine::serialize_trace_query;
using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::index_blocks;
using pnga::deflate_trace::TokenDecodeResult;
using pnga::deflate_trace::decode_stored_and_fixed;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_format::VirtualIDATStream;
using pnga::png_format::index_chunks;
using pnga::trace_model::Selection;

namespace {

class VirtualSource final : public IByteSource {
 public:
  VirtualSource(const VirtualIDATStream& stream, const IByteSource& file)
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
  const VirtualIDATStream& stream_;
  const IByteSource& file_;
};

struct TraceInputs {
  MemoryByteSource file;
  pnga::png_format::ChunkIndex chunks;
  VirtualIDATStream stream;
  VirtualSource logical;
  BlockIndexResult blocks;
  TokenDecodeResult trace;

  explicit TraceInputs(std::vector<std::byte> bytes)
      : file(std::move(bytes)),
        chunks(index_chunks(file)),
        stream(chunks),
        logical(stream, file),
        blocks(index_blocks(logical, 1u << 20)),
        trace(decode_stored_and_fixed(logical, 1u << 20)) {}

  explicit TraceInputs(const pnga_test::EncodedPng& encoded)
      : TraceInputs(encoded.png_bytes) {}
};

std::vector<std::byte> split_idat_payload(const pnga_test::EncodedPng& encoded) {
  MemoryByteSource original(encoded.png_bytes);
  const auto index = index_chunks(original);
  std::vector<std::byte> ihdr;
  std::vector<std::byte> compressed;
  for (const auto& chunk : index.chunks) {
    if (chunk.text() == "IHDR") {
      ihdr.resize(static_cast<std::size_t>(chunk.data_length));
      REQUIRE(original.read(chunk.data_offset, ihdr.data(), ihdr.size()));
    } else if (chunk.text() == "IDAT") {
      const auto old_size = compressed.size();
      compressed.resize(old_size + static_cast<std::size_t>(chunk.data_length));
      REQUIRE(original.read(chunk.data_offset, compressed.data() + old_size,
                            static_cast<std::size_t>(chunk.data_length)));
    }
  }
  REQUIRE_FALSE(ihdr.empty());
  REQUIRE(compressed.size() > 2);

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  const auto append_chunk = [&png](const char* type,
                                   const std::vector<std::byte>& data) {
    const auto length = static_cast<std::uint32_t>(data.size());
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(length)));
    const auto type_bytes = reinterpret_cast<const Bytef*>(type);
    uLong crc = crc32(0, type_bytes, 4);
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    for (int i = 0; i < 4; ++i) {
      png.push_back(pnga_test::B(static_cast<unsigned char>(type[i])));
    }
    png.insert(png.end(), data.begin(), data.end());
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 24)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 16)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc >> 8)));
    png.push_back(pnga_test::B(static_cast<unsigned char>(crc)));
  };
  append_chunk("IHDR", ihdr);
  const auto split = compressed.size() / 2;
  append_chunk("IDAT", std::vector<std::byte>(compressed.begin(),
                                                compressed.begin() + split));
  append_chunk("IDAT", std::vector<std::byte>(compressed.begin() + split,
                                                compressed.end()));
  append_chunk("IEND", {});
  return png;
}

}  // namespace

TEST_CASE("Trace query status text is stable", "[analysis-engine][wp5t0a]") {
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kNotIndexed)) == "not indexed");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kReplaying)) == "replaying");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kReady)) == "ready");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kPartial)) == "partial");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kError)) == "error");
  REQUIRE(std::string(pnga::analysis_engine::trace_query_status_text(
              TraceQueryStatus::kCancelled)) == "cancelled");
}

TEST_CASE("Trace query composes a ready bounded result",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);

  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      9, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE_FALSE(result.blocks.empty());
  REQUIRE_FALSE(result.tokens.empty());
  REQUIRE_FALSE(result.logical_input.empty());
  REQUIRE_FALSE(result.physical_input.empty());
  REQUIRE(result.output_bytes == inputs.trace.output_bytes);
  REQUIRE(std::all_of(result.tokens.begin(), result.tokens.end(),
                       [](const auto& token) {
                         return token.input_bit_begin <= token.input_bit_end &&
                                token.output_begin <= token.output_end;
                       }));
}

TEST_CASE("Trace query preserves physical spans across multiple IDAT chunks",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(split_idat_payload(encoded));
  REQUIRE(inputs.stream.segment_count() == 2);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);
  const auto result = compose_trace_query(
      3, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE(result.physical_input.size() >= 2);
}

TEST_CASE("Trace query preserves verified data as a partial result",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(32, 32, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  TraceInputs inputs(encoded);
  REQUIRE(inputs.blocks.success);
  REQUIRE(inputs.trace.success);

  auto unavailable_trace = inputs.trace;
  unavailable_trace.success = false;
  unavailable_trace.error = "replay stopped at block boundary";
  const auto partial = compose_trace_query(
      4, Selection{}, inputs.blocks, unavailable_trace, inputs.stream,
      inputs.file, 0, inputs.blocks.total_output_bytes, 64);
  REQUIRE(partial.status == TraceQueryStatus::kPartial);
  REQUIRE(partial.error == "replay stopped at block boundary");
  REQUIRE_FALSE(partial.blocks.empty());
  // A failed replay can still carry verified prefix artifacts; those are
  // retained so the UI can render a useful partial trace.
  REQUIRE_FALSE(partial.tokens.empty());

  auto unavailable_index = inputs.blocks;
  unavailable_index.success = false;
  unavailable_index.error = "trace index not available";
  const auto index_partial = compose_trace_query(
      4, Selection{}, unavailable_index, inputs.trace, inputs.stream,
      inputs.file, 0, inputs.trace.output_bytes, 64);
  REQUIRE(index_partial.status == TraceQueryStatus::kPartial);
  REQUIRE(index_partial.error == "trace index not available");
  // A failed index may still carry verified block ranges; the contract keeps
  // those ranges visible while marking the result partial.
  REQUIRE_FALSE(index_partial.blocks.empty());
  REQUIRE_FALSE(index_partial.tokens.empty());
}

TEST_CASE("Trace token budget and invalid ranges have stable errors",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(64, 32, 8, 2, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  const auto partial = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 1);
  REQUIRE(partial.status == TraceQueryStatus::kPartial);
  REQUIRE(partial.truncated);
  REQUIRE(partial.tokens.size() == 1);
  REQUIRE(partial.error == "trace token budget exceeded");

  const auto invalid = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      inputs.trace.output_bytes, 0, 1);
  REQUIRE(invalid.status == TraceQueryStatus::kError);
  REQUIRE(invalid.error == "inflated query range is out of bounds");

  const auto zero_budget = compose_trace_query(
      1, Selection{}, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 0);
  REQUIRE(zero_budget.status == TraceQueryStatus::kError);
  REQUIRE(zero_budget.error == "trace token budget must be non-zero");
}

TEST_CASE("Trace query serialization is deterministic and complete",
          "[analysis-engine][wp5t0a]") {
  const auto encoded =
      pnga_test::encode_png(32, 16, 8, 6, /*interlace=*/false,
                            /*all_none=*/false);
  TraceInputs inputs(encoded);
  Selection selection;
  selection.node = 17;
  selection.stage = pnga::trace_model::Stage::kTrace;
  const auto result = compose_trace_query(
      11, selection, inputs.blocks, inputs.trace, inputs.stream, inputs.file,
      0, inputs.trace.output_bytes, 8);
  const auto first = serialize_trace_query(result);
  const auto second = serialize_trace_query(result);
  REQUIRE(first == second);
  REQUIRE(first.starts_with("trace-query-v1\nstatus:partial\n"));
  REQUIRE(first.find("generation:11\n") != std::string::npos);
  REQUIRE(first.find("selection:node:17;stage:trace\n") !=
          std::string::npos);
  REQUIRE(first.find("blocks:") != std::string::npos);
  REQUIRE(first.find("tokens:") != std::string::npos);
  REQUIRE(first.find("tables:") != std::string::npos);
  REQUIRE(first.find("logical:") != std::string::npos);
  REQUIRE(first.find("physical:") != std::string::npos);
  REQUIRE(first.find("matches:") != std::string::npos);
}

TEST_CASE("Trace query serialization names every Deflate block type",
          "[analysis-engine][wp5t0a]") {
  pnga::analysis_engine::TraceQueryResult result;
  result.status = TraceQueryStatus::kReady;
  result.blocks = {
      {0, pnga::deflate_index::BlockType::kStored, false, 0, 8, 0, 1, {}},
      {1, pnga::deflate_index::BlockType::kFixed, false, 8, 16, 1, 2, {}},
      {2, pnga::deflate_index::BlockType::kDynamic, true, 16, 24, 2, 3, {}},
  };
  const auto serialized = serialize_trace_query(result);
  REQUIRE(serialized.find("block:0,stored,") != std::string::npos);
  REQUIRE(serialized.find("block:1,fixed,") != std::string::npos);
  REQUIRE(serialized.find("block:2,dynamic,") != std::string::npos);
}
