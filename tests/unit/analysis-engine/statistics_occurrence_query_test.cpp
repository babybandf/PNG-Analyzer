// WP-602F: bounded statistics occurrence navigation tests. Chunk, filter and
// block domains resolve directly from the existing indexes; token, length and
// distance domains run a bounded scalar scan that retains no occurrence list.
// Every result maps the matched fact to the existing typed Selection with
// every cross-IDAT physical span.

#include <pnga/analysis-engine/statistics_occurrence_query.h>

#include <catch2/catch_test_macros.hpp>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/selection.h>

#include "controlled_fixture.h"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::OccurrenceDirection;
using pnga::analysis_engine::OccurrenceStatus;
using pnga::analysis_engine::StatisticsBucketDomain;
using pnga::analysis_engine::StatisticsNavigationRequest;
using pnga::analysis_engine::StatisticsOccurrenceResult;
using pnga::analysis_engine::StageSet;
using pnga::analysis_engine::CancellationToken;
using pnga::analysis_engine::query_statistics_occurrence;
using pnga::deflate_index::index_blocks;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::index_chunks;
using pnga::png_format::VirtualIDATStream;
using pnga_test::wp607c::ControlledCaseId;
using pnga_test::wp607c::make_controlled_fixture;

// Read-only adapter exposing the virtual IDAT stream to the block index
// without concatenating payload bytes.
class IdatByteSource final : public IByteSource {
 public:
  IdatByteSource(const VirtualIDATStream& stream, const IByteSource& file)
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
  const IByteSource& file_;
};

struct QueryFixture {
  std::vector<std::byte> bytes;
  std::shared_ptr<MemoryByteSource> source;
  ChunkIndex chunks;
  std::shared_ptr<const StageSet> stages;
  pnga::deflate_index::BlockIndexResult blocks;
};

QueryFixture make_query_fixture(ControlledCaseId id) {
  QueryFixture fixture;
  fixture.bytes = make_controlled_fixture(id).png_bytes;
  fixture.source = std::make_shared<MemoryByteSource>(fixture.bytes);
  fixture.chunks = index_chunks(*fixture.source);
  fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*fixture.source));
  VirtualIDATStream stream(fixture.chunks);
  IdatByteSource logical(stream, *fixture.source);
  fixture.blocks = index_blocks(logical, 1u << 22);
  REQUIRE(fixture.blocks.success);
  return fixture;
}

StatisticsNavigationRequest make_request(StatisticsBucketDomain domain,
                                         const std::string& key,
                                         OccurrenceDirection direction =
                                             OccurrenceDirection::kFirst,
                                         std::optional<std::uint64_t> cursor =
                                             std::nullopt) {
  StatisticsNavigationRequest request;
  request.generation = 5;
  request.domain = domain;
  request.key = key;
  request.direction = direction;
  request.after_output_offset = cursor;
  return request;
}

// Test-side oracle: maps a DEFLATE bit range through the wrapper origin and
// the virtual IDAT stream to the ordered physical file spans.
std::vector<pnga::trace_model::BitSpan> expected_physical_spans(
    const ChunkIndex& chunks, std::uint64_t wrapper_bits,
    std::uint64_t deflate_bit_begin, std::uint64_t deflate_bit_end) {
  const std::uint64_t logical_begin = (wrapper_bits + deflate_bit_begin) / 8;
  const std::uint64_t logical_end = (wrapper_bits + deflate_bit_end + 7) / 8;
  VirtualIDATStream stream(chunks);
  std::vector<pnga::png_format::PhysicalRange> ranges;
  REQUIRE(stream.logical_to_physical(
      logical_begin, logical_end - logical_begin, ranges));
  std::vector<pnga::trace_model::BitSpan> spans;
  for (const auto& range : ranges) {
    spans.push_back(
        pnga::trace_model::BitSpan{range.offset, range.length, 0, false});
  }
  return spans;
}

void require_ready_selection(const StatisticsOccurrenceResult& result,
                             std::uint64_t deflate_bit_begin,
                             std::uint64_t deflate_bit_end,
                             const QueryFixture& fixture) {
  REQUIRE(result.status == OccurrenceStatus::kReady);
  REQUIRE(result.generation == 5);
  REQUIRE(result.error.empty());
  REQUIRE(result.selection.stage == pnga::trace_model::Stage::kTrace);
  // The exact IDAT-logical byte envelope of the matched token.
  const std::uint64_t logical_begin =
      (fixture.blocks.zlib_header_bits + deflate_bit_begin) / 8;
  const std::uint64_t logical_end =
      (fixture.blocks.zlib_header_bits + deflate_bit_end + 7) / 8;
  REQUIRE(result.selection.logical.has_value());
  REQUIRE(result.selection.logical->start == logical_begin);
  REQUIRE(result.selection.logical->length == logical_end - logical_begin);
  // Every ordered cross-IDAT physical span of that envelope.
  REQUIRE(result.selection.physical_spans ==
          expected_physical_spans(fixture.chunks,
                                  fixture.blocks.zlib_header_bits,
                                  deflate_bit_begin, deflate_bit_end));
  REQUIRE(result.searched_tokens >= 1);
  REQUIRE(result.searched_input_bytes > 0);
}

// A read-only file source that requests cancellation after a fixed number
// of reads, so a scalar scan is cancelled mid-stream deterministically (the
// query builds its own virtual IDAT adapter on top of this file source).
class CancelAfterReadsSource final : public IByteSource {
 public:
  CancelAfterReadsSource(const IByteSource& file, CancellationToken& token,
                         std::uint64_t after_reads)
      : file_(file), token_(token), after_(after_reads) {}

  std::uint64_t size() const noexcept override { return file_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (++reads_ >= after_) {
      token_.request_cancel();
    }
    return file_.read(offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const IByteSource& file_;
  CancellationToken& token_;
  std::uint64_t after_;
  mutable std::uint64_t reads_ = 0;
};

// Builds a wide gray8 level-0 stored PNG (deterministic literal count).
std::vector<std::byte> wide_stored_png(std::uint32_t w, std::uint32_t h) {
  std::vector<std::byte> filtered;
  for (std::uint32_t y = 0; y < h; ++y) {
    filtered.push_back(std::byte{0});
    for (std::uint32_t x = 0; x < w; ++x) {
      filtered.push_back(static_cast<std::byte>(1 + ((x * 3 + y * 7) % 250)));
    }
  }
  z_stream strm{};
  if (deflateInit2(&strm, 0, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(filtered.size()));
  std::vector<std::byte> comp(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(filtered.data());
  strm.avail_in = static_cast<uInt>(filtered.size());
  strm.next_out = reinterpret_cast<Bytef*>(comp.data());
  strm.avail_out = static_cast<uInt>(comp.size());
  const int rc = deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  if (rc != Z_STREAM_END) {
    return {};
  }
  comp.resize(strm.total_out);

  std::vector<std::byte> bytes(pnga::png_format::kPngSignature.begin(),
                               pnga::png_format::kPngSignature.end());
  const auto push = [&bytes](const char* type,
                             const std::vector<std::byte>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(std::byte(static_cast<unsigned char>((len >> shift) & 0xFFu)));
    }
    uLong crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(std::byte(static_cast<unsigned char>(type[i])));
    }
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uInt>(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(std::byte(static_cast<unsigned char>((crc >> shift) & 0xFFu)));
    }
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = std::byte(static_cast<unsigned char>((w >> 24) & 0xFFu));
  ihdr[1] = std::byte(static_cast<unsigned char>((w >> 16) & 0xFFu));
  ihdr[2] = std::byte(static_cast<unsigned char>((w >> 8) & 0xFFu));
  ihdr[3] = std::byte(static_cast<unsigned char>(w & 0xFFu));
  ihdr[4] = std::byte(static_cast<unsigned char>((h >> 24) & 0xFFu));
  ihdr[5] = std::byte(static_cast<unsigned char>((h >> 16) & 0xFFu));
  ihdr[6] = std::byte(static_cast<unsigned char>((h >> 8) & 0xFFu));
  ihdr[7] = std::byte(static_cast<unsigned char>(h & 0xFFu));
  ihdr[8] = std::byte{8};
  ihdr[9] = std::byte{0};
  push("IHDR", ihdr);
  push("IDAT", comp);
  push("IEND", {});
  return bytes;
}

}  // namespace

TEST_CASE("Chunk navigation resolves first, next and previous from the index",
          "[analysis-engine][wp602f]") {
  // Two IDAT chunks: next/previous walk the physical chunk sequence.
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kIdatSplitZlibHeader);

  const auto first = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "IDAT"), nullptr);
  REQUIRE(first.status == OccurrenceStatus::kReady);
  REQUIRE(first.generation == 5);
  REQUIRE(first.selection.stage == pnga::trace_model::Stage::kChunk);
  REQUIRE(first.selection.node.has_value());
  const auto& idat_first = fixture.chunks.chunks[*first.selection.node];
  REQUIRE(idat_first.text() == "IDAT");
  REQUIRE(first.selection.physical_spans.size() == 3);
  REQUIRE(first.selection.physical_spans[0].offset ==
          idat_first.header_offset);
  REQUIRE(first.selection.physical_spans[0].length == 8);
  REQUIRE(first.selection.physical_spans[1].offset == idat_first.data_offset);
  REQUIRE(first.selection.physical_spans[1].length ==
          idat_first.data_length);
  REQUIRE(first.selection.physical_spans[2].offset == idat_first.crc_offset);
  REQUIRE(first.selection.physical_spans[2].length == 4);

  const auto next = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "IDAT",
                   OccurrenceDirection::kNext, idat_first.header_offset),
      nullptr);
  REQUIRE(next.status == OccurrenceStatus::kReady);
  REQUIRE(next.selection.node.has_value());
  REQUIRE(*next.selection.node != *first.selection.node);
  const auto& idat_second = fixture.chunks.chunks[*next.selection.node];
  REQUIRE(idat_second.text() == "IDAT");
  REQUIRE(idat_second.header_offset > idat_first.header_offset);

  const auto previous = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "IDAT",
                   OccurrenceDirection::kPrevious, idat_second.header_offset),
      nullptr);
  REQUIRE(previous.status == OccurrenceStatus::kReady);
  REQUIRE(previous.selection.node == first.selection.node);

  // No chunk before the first one.
  const auto no_previous = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "IDAT",
                   OccurrenceDirection::kPrevious, idat_first.header_offset),
      nullptr);
  REQUIRE(no_previous.status == OccurrenceStatus::kNotFound);
  // No chunk after the last one.
  const auto no_next = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "IDAT",
                   OccurrenceDirection::kNext, idat_second.header_offset),
      nullptr);
  REQUIRE(no_next.status == OccurrenceStatus::kNotFound);
  // A type that does not exist in the document.
  const auto missing = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kChunkType, "tEXt"), nullptr);
  REQUIRE(missing.status == OccurrenceStatus::kNotFound);
}

TEST_CASE("Filter navigation resolves scanline occurrences from the anchors",
          "[analysis-engine][wp602f]") {
  // Each filter type 0..4 appears exactly once: first resolves the matching
  // scanline ordinal.
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kUiRgb8FiveFilters);
  REQUIRE(fixture.stages->success);
  REQUIRE(fixture.stages->scanlines.size() == 5);

  for (const std::string& key : {"0", "1", "2", "3", "4"}) {
    const auto first = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kFilterType, key), nullptr);
    CAPTURE(key);
    REQUIRE(first.status == OccurrenceStatus::kReady);
    REQUIRE(first.generation == 5);
    REQUIRE(first.selection.stage == pnga::trace_model::Stage::kFiltered);
    REQUIRE(first.selection.image.has_value());
    REQUIRE(first.selection.image->row ==
            static_cast<std::uint64_t>(std::stoull(key)));
    // The compressed provenance maps to physical file bytes.
    REQUIRE_FALSE(first.selection.physical_spans.empty());
    // The single occurrence has no successor.
    const std::uint64_t offset =
        fixture.stages->scanlines[static_cast<std::size_t>(
                                      first.selection.image->row)]
            .offset;
    const auto next = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kFilterType, key,
                     OccurrenceDirection::kNext, offset),
        nullptr);
    REQUIRE(next.status == OccurrenceStatus::kNotFound);
  }

  // Four scanlines of filter type 0: next/previous walk the scanline order.
  std::vector<std::byte> multi = wide_stored_png(8, 4);
  QueryFixture multi_fixture;
  multi_fixture.bytes = std::move(multi);
  multi_fixture.source =
      std::make_shared<MemoryByteSource>(multi_fixture.bytes);
  multi_fixture.chunks = index_chunks(*multi_fixture.source);
  multi_fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*multi_fixture.source));
  VirtualIDATStream multi_stream(multi_fixture.chunks);
  IdatByteSource multi_logical(multi_stream, *multi_fixture.source);
  multi_fixture.blocks = index_blocks(multi_logical, 1u << 22);
  REQUIRE(multi_fixture.blocks.success);
  REQUIRE(multi_fixture.stages->scanlines.size() == 4);

  const auto first = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, multi_fixture.stages.get(),
      &multi_fixture.blocks,
      make_request(StatisticsBucketDomain::kFilterType, "0"), nullptr);
  REQUIRE(first.status == OccurrenceStatus::kReady);
  REQUIRE(first.selection.image->row == 0);

  const auto next = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, multi_fixture.stages.get(),
      &multi_fixture.blocks,
      make_request(StatisticsBucketDomain::kFilterType, "0",
                   OccurrenceDirection::kNext,
                   multi_fixture.stages->scanlines[0].offset),
      nullptr);
  REQUIRE(next.status == OccurrenceStatus::kReady);
  REQUIRE(next.selection.image->row == 1);

  const auto previous = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, multi_fixture.stages.get(),
      &multi_fixture.blocks,
      make_request(StatisticsBucketDomain::kFilterType, "0",
                   OccurrenceDirection::kPrevious,
                   multi_fixture.stages->scanlines[1].offset),
      nullptr);
  REQUIRE(previous.status == OccurrenceStatus::kReady);
  REQUIRE(previous.selection.image->row == 0);

  // A filter type that has no row in this document.
  const auto missing = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, multi_fixture.stages.get(),
      &multi_fixture.blocks,
      make_request(StatisticsBucketDomain::kFilterType, "4"), nullptr);
  REQUIRE(missing.status == OccurrenceStatus::kNotFound);

  // Missing facts make the query fail with a stable message.
  const auto no_stages = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, nullptr, &multi_fixture.blocks,
      make_request(StatisticsBucketDomain::kFilterType, "0"), nullptr);
  REQUIRE(no_stages.status == OccurrenceStatus::kError);
  REQUIRE_FALSE(no_stages.error.empty());
  const auto no_blocks = query_statistics_occurrence(
      *multi_fixture.source, multi_fixture.chunks, multi_fixture.stages.get(),
      nullptr,
      make_request(StatisticsBucketDomain::kFilterType, "0"), nullptr);
  REQUIRE(no_blocks.status == OccurrenceStatus::kError);
}

TEST_CASE("Block navigation resolves first, next and previous from the index",
          "[analysis-engine][wp602f]") {
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kTraceMultiblockBfinal);

  const auto first = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kBlockType, "stored"), nullptr);
  REQUIRE(first.status == OccurrenceStatus::kReady);
  REQUIRE(first.selection.stage == pnga::trace_model::Stage::kTrace);
  REQUIRE(first.selection.logical.has_value());
  // Stored block 0 starts at deflate bit 3 (after BFINAL+BTYPE), i.e. logical
  // byte 2 of the IDAT stream.
  REQUIRE(first.selection.logical->start == 2);
  REQUIRE_FALSE(first.selection.physical_spans.empty());

  // Dynamic and fixed blocks resolve directly too.
  for (const char* type : {"fixed", "dynamic"}) {
    const auto block = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kBlockType, type), nullptr);
    REQUIRE(block.status == OccurrenceStatus::kReady);
    REQUIRE(block.selection.logical.has_value());
  }

  // Two same-type stored blocks: next/previous walk the inflated output.
  std::vector<std::byte> wide = wide_stored_png(70'000, 1);
  QueryFixture wide_fixture;
  wide_fixture.bytes = std::move(wide);
  wide_fixture.source = std::make_shared<MemoryByteSource>(wide_fixture.bytes);
  wide_fixture.chunks = index_chunks(*wide_fixture.source);
  wide_fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*wide_fixture.source));
  VirtualIDATStream wide_stream(wide_fixture.chunks);
  IdatByteSource wide_logical(wide_stream, *wide_fixture.source);
  wide_fixture.blocks = index_blocks(wide_logical, 1u << 24);
  REQUIRE(wide_fixture.blocks.success);
  REQUIRE(wide_fixture.blocks.blocks.size() >= 2);
  REQUIRE(wide_fixture.blocks.blocks[0].type ==
          pnga::deflate_index::BlockType::kStored);
  REQUIRE(wide_fixture.blocks.blocks[1].type ==
          pnga::deflate_index::BlockType::kStored);

  const auto stored_first = query_statistics_occurrence(
      *wide_fixture.source, wide_fixture.chunks, wide_fixture.stages.get(),
      &wide_fixture.blocks,
      make_request(StatisticsBucketDomain::kBlockType, "stored"), nullptr);
  REQUIRE(stored_first.status == OccurrenceStatus::kReady);
  REQUIRE(stored_first.selection.logical->start == 2);

  const auto stored_next = query_statistics_occurrence(
      *wide_fixture.source, wide_fixture.chunks, wide_fixture.stages.get(),
      &wide_fixture.blocks,
      make_request(StatisticsBucketDomain::kBlockType, "stored",
                   OccurrenceDirection::kNext,
                   wide_fixture.blocks.blocks[0].output_begin),
      nullptr);
  REQUIRE(stored_next.status == OccurrenceStatus::kReady);
  REQUIRE(stored_next.selection.logical.has_value());
  REQUIRE(stored_next.selection.logical->start ==
          wide_fixture.blocks.blocks[1].input_bit_begin / 8);

  const auto stored_previous = query_statistics_occurrence(
      *wide_fixture.source, wide_fixture.chunks, wide_fixture.stages.get(),
      &wide_fixture.blocks,
      make_request(StatisticsBucketDomain::kBlockType, "stored",
                   OccurrenceDirection::kPrevious,
                   wide_fixture.blocks.blocks[1].output_begin),
      nullptr);
  REQUIRE(stored_previous.status == OccurrenceStatus::kReady);
  REQUIRE(stored_previous.selection.logical->start == 2);

  const auto missing = query_statistics_occurrence(
      *wide_fixture.source, wide_fixture.chunks, wide_fixture.stages.get(),
      &wide_fixture.blocks,
      make_request(StatisticsBucketDomain::kBlockType, "dynamic"), nullptr);
  REQUIRE(missing.status == OccurrenceStatus::kNotFound);
}

TEST_CASE("Token navigation finds matches on stored, fixed and dynamic "
          "streams",
          "[analysis-engine][wp602f]") {
  // Stored: the first literal at output 0.
  {
    const QueryFixture fixture =
        make_query_fixture(ControlledCaseId::kTraceStoredLiterals);
    const auto stored_fixture =
        make_controlled_fixture(ControlledCaseId::kTraceStoredLiterals);
    const auto& fact = stored_fixture.expected.tokens[0];
    REQUIRE(fact.kind == pnga_test::wp607c::TokenKind::kLiteral);
    const auto first = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kTokenKind, "literal"), nullptr);
    require_ready_selection(first, fact.input_bits.begin, fact.input_bits.end,
                            fixture);
    // No length tokens exist on a stored stream.
    const auto no_match = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kLength, "3"), nullptr);
    REQUIRE(no_match.status == OccurrenceStatus::kNotFound);
  }
  // Fixed: the match with length 3 distance 3 at output 3.
  {
    const QueryFixture fixture =
        make_query_fixture(ControlledCaseId::kTraceFixedNonoverlap);
    const auto facts = make_controlled_fixture(
        ControlledCaseId::kTraceFixedNonoverlap).expected.tokens;
    const auto first = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kLength, "3"), nullptr);
    require_ready_selection(first, facts[3].input_bits.begin,
                            facts[3].input_bits.end, fixture);
    const auto by_distance = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kDistance, "3"), nullptr);
    REQUIRE(by_distance.status == OccurrenceStatus::kReady);
    REQUIRE(by_distance.selection == first.selection);
    const auto by_kind = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kTokenKind, "match"), nullptr);
    REQUIRE(by_kind.status == OccurrenceStatus::kReady);
    REQUIRE(by_kind.selection == first.selection);
  }
  // Dynamic: the overlapping length-6 distance-1 match at output 2.
  {
    const QueryFixture fixture =
        make_query_fixture(ControlledCaseId::kTraceDynamicOverlapRepeats);
    const auto facts = make_controlled_fixture(
        ControlledCaseId::kTraceDynamicOverlapRepeats).expected.tokens;
    const auto first = query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        make_request(StatisticsBucketDomain::kLength, "6"), nullptr);
    require_ready_selection(first, facts[2].input_bits.begin,
                            facts[2].input_bits.end, fixture);
  }
}

TEST_CASE("Token navigation walks first, next and previous around a cursor",
          "[analysis-engine][wp602f]") {
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kTraceFixedNonoverlap);
  // Tokens: literals at outputs 0,1,2; match at output 3; eob at output 6.

  // first literal, then next literal after output 0, then after output 1.
  const auto first = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal"), nullptr);
  REQUIRE(first.status == OccurrenceStatus::kReady);
  const auto second = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kNext, 0),
      nullptr);
  REQUIRE(second.status == OccurrenceStatus::kReady);
  REQUIRE(second.selection != first.selection);
  const auto third = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kNext, 1),
      nullptr);
  REQUIRE(third.status == OccurrenceStatus::kReady);
  REQUIRE(third.selection != second.selection);

  // The cursor is exclusive: the literal at output 1 is skipped.
  const auto skip = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kNext, 1),
      nullptr);
  REQUIRE(skip.status == OccurrenceStatus::kReady);
  REQUIRE(skip.selection == third.selection);

  // previous literal before output 2 is the literal at output 1.
  const auto previous = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kPrevious, 2),
      nullptr);
  REQUIRE(previous.status == OccurrenceStatus::kReady);
  REQUIRE(previous.selection == second.selection);

  // previous before output 0 finds nothing.
  const auto no_previous = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kPrevious, 0),
      nullptr);
  REQUIRE(no_previous.status == OccurrenceStatus::kNotFound);

  // next after the last match finds nothing.
  const auto no_next = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal",
                   OccurrenceDirection::kNext, 2),
      nullptr);
  REQUIRE(no_next.status == OccurrenceStatus::kNotFound);
}

TEST_CASE("Token occurrence scans are bounded and report the searched range",
          "[analysis-engine][wp602f]") {
  // More than 4,096 tokens with no length-3 match: the frozen token budget
  // truncates the scan to a verified partial with the searched range.
  std::vector<std::byte> many = wide_stored_png(5'000, 1);
  QueryFixture fixture;
  fixture.bytes = std::move(many);
  fixture.source = std::make_shared<MemoryByteSource>(fixture.bytes);
  fixture.chunks = index_chunks(*fixture.source);
  fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*fixture.source));
  VirtualIDATStream stream(fixture.chunks);
  IdatByteSource logical(stream, *fixture.source);
  fixture.blocks = index_blocks(logical, 1u << 22);
  REQUIRE(fixture.blocks.success);

  const auto truncated = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kLength, "3"), nullptr);
  REQUIRE(truncated.status == OccurrenceStatus::kPartial);
  REQUIRE(truncated.generation == 5);
  REQUIRE(truncated.searched_tokens == 4096);
  REQUIRE(truncated.selection.empty());
  REQUIRE_FALSE(truncated.error.empty());

  // An explicit input budget truncates on consumed input bytes.
  StatisticsNavigationRequest input_bounded = make_request(
      StatisticsBucketDomain::kLength, "3");
  input_bounded.max_tokens = 0;  // unlimited tokens
  input_bounded.max_input_bytes = 4096;
  const auto input_truncated = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      input_bounded, nullptr);
  REQUIRE(input_truncated.status == OccurrenceStatus::kPartial);
  // The decoder checks the input budget before each token, so the searched
  // range can exceed the cap by at most the last token's input width.
  REQUIRE(input_truncated.searched_input_bytes <= 4096 + 1);
  REQUIRE(input_truncated.searched_input_bytes > 0);

  // Cancellation wins over any collected evidence.
  CancellationToken token;
  token.request_cancel();
  const auto cancelled = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal"), &token);
  REQUIRE(cancelled.status == OccurrenceStatus::kCancelled);
  REQUIRE(cancelled.selection.empty());
}

TEST_CASE("Token occurrence cancellation mid-scan keeps no selection",
          "[analysis-engine][wp602f]") {
  std::vector<std::byte> many = wide_stored_png(5'000, 1);
  QueryFixture fixture;
  fixture.bytes = std::move(many);
  fixture.source = std::make_shared<MemoryByteSource>(fixture.bytes);
  fixture.chunks = index_chunks(*fixture.source);
  fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*fixture.source));
  VirtualIDATStream stream(fixture.chunks);
  IdatByteSource direct(stream, *fixture.source);
  fixture.blocks = index_blocks(direct, 1u << 22);
  REQUIRE(fixture.blocks.success);

  // The wrapper file source requests cancellation after a few reads so the
  // scalar scan is cancelled mid-stream (never a read failure: every read is
  // still served).
  CancellationToken token;
  CancelAfterReadsSource cancelling(*fixture.source, token,
                                    /*after_reads=*/3);

  pnga::analysis_engine::StatisticsNavigationRequest request =
      make_request(StatisticsBucketDomain::kLength, "3");
  request.generation = 5;
  const auto result = query_statistics_occurrence(
      cancelling, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      request, &token);
  CAPTURE(result.error);
  REQUIRE(result.status == OccurrenceStatus::kCancelled);
  REQUIRE(result.generation == 5);
  REQUIRE(result.selection.empty());
}

TEST_CASE("Invalid occurrence requests fail with stable errors",
          "[analysis-engine][wp602f]") {
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kTraceFixedNonoverlap);
  const auto run = [&](const StatisticsNavigationRequest& request) {
    return query_statistics_occurrence(
        *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
        request, nullptr);
  };
  // Unknown bucket keys.
  REQUIRE(run(make_request(StatisticsBucketDomain::kTokenKind, "symbol"))
              .status == OccurrenceStatus::kError);
  REQUIRE(run(make_request(StatisticsBucketDomain::kBlockType, "reserved"))
              .status == OccurrenceStatus::kError);
  REQUIRE(run(make_request(StatisticsBucketDomain::kFilterType, "9"))
              .status == OccurrenceStatus::kError);
  REQUIRE(run(make_request(StatisticsBucketDomain::kLength, "abc"))
              .status == OccurrenceStatus::kError);
  REQUIRE(run(make_request(StatisticsBucketDomain::kChunkType, "IDAT"))
              .status == OccurrenceStatus::kReady);
  // A stale generation is echoed so the caller can reject it.
  StatisticsNavigationRequest stale =
      make_request(StatisticsBucketDomain::kTokenKind, "literal");
  stale.generation = 12;
  const auto result = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      stale, nullptr);
  REQUIRE(result.generation == 12);
  REQUIRE(result.generation != 5);
}

TEST_CASE("Occurrence physical spans cover tokens that cross IDAT chunks",
          "[analysis-engine][wp602f]") {
  // The 'A' literal spans logical bytes [2,4); the IDAT boundary at byte 3
  // splits the token across two physical chunks.
  const QueryFixture fixture =
      make_query_fixture(ControlledCaseId::kIdatSplitToken);
  REQUIRE(fixture.chunks.chunks.size() > 2);
  const auto first = query_statistics_occurrence(
      *fixture.source, fixture.chunks, fixture.stages.get(), &fixture.blocks,
      make_request(StatisticsBucketDomain::kTokenKind, "literal"), nullptr);
  REQUIRE(first.status == OccurrenceStatus::kReady);
  REQUIRE(first.selection.physical_spans.size() == 2);
  // The two spans tile the logical envelope [2,4) in order.
  REQUIRE(first.selection.logical.has_value());
  REQUIRE(first.selection.logical->start == 2);
  REQUIRE(first.selection.logical->length == 2);
  REQUIRE(first.selection.physical_spans[0].length +
              first.selection.physical_spans[1].length ==
          2);
  REQUIRE(first.selection.physical_spans[0].offset <
          first.selection.physical_spans[1].offset);
}
