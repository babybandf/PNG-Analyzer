// WP-602C: whole-document statistics collector tests. The collector combines
// cached Chunk/Filter/Block facts with the streaming scalar token pass, is
// cancelable at section boundaries and inside the token scan, throttles
// progress through an injected monotonic clock and maps every lower-level
// result to the frozen section statuses without upgrading partial results.

#include <pnga/analysis-engine/statistics_collector.h>

#include <catch2/catch_test_macros.hpp>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/statistics_adapter.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/statistics/statistics.h>

#include "controlled_fixture.h"
#include "test_png_helpers.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::CancellationToken;
using pnga::analysis_engine::StageSet;
using pnga::analysis_engine::StatisticsCollectionRequest;
using pnga::analysis_engine::StatisticsCollectionResult;
using pnga::analysis_engine::StatisticsProgress;
using pnga::analysis_engine::collect_document_statistics;
using pnga::analysis_engine::collect_statistics;
using pnga::deflate_index::index_blocks;
using pnga::deflate_trace::decode_stored_and_fixed;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_format::index_chunks;
using pnga::png_format::VirtualIDATStream;
using pnga::statistics::BlockKind;
using pnga::statistics::BlockSample;
using pnga::statistics::ChunkSample;
using pnga::statistics::FilterSample;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsInput;
using pnga::statistics::StatisticsLimits;
using pnga::statistics::StatisticsSnapshot;
using pnga::statistics::TokenKind;
using pnga::statistics::TokenSample;
using SId = pnga::statistics::StatisticsSectionId;

// A read-only adapter exposing the virtual IDAT stream to the test-side
// oracle without concatenating payload bytes.
class OracleIdatSource final : public IByteSource {
 public:
  OracleIdatSource(const VirtualIDATStream& stream, const IByteSource& file)
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

struct Fixture {
  std::vector<std::byte> bytes;
  std::shared_ptr<MemoryByteSource> source;
  pnga::png_format::ChunkIndex chunks;
  std::shared_ptr<const StageSet> stages;
};

Fixture make_fixture(const std::vector<std::byte>& png_bytes) {
  Fixture fixture;
  fixture.bytes = png_bytes;
  fixture.source = std::make_shared<MemoryByteSource>(fixture.bytes);
  fixture.chunks = index_chunks(*fixture.source);
  fixture.stages = std::make_shared<const StageSet>(
      pnga::analysis_engine::analyze_source(*fixture.source));
  return fixture;
}

StatisticsCollectionRequest make_request(const Fixture& fixture,
                                         std::uint64_t generation = 7) {
  StatisticsCollectionRequest request;
  request.generation = generation;
  request.source = fixture.source;
  request.chunks = fixture.chunks;
  request.stages = fixture.stages;
  request.limits = StatisticsLimits{};
  request.max_working_bytes = 64ull << 20;
  return request;
}

// Deterministic monotonic clock: every call advances the logical time by
// `step_ms`. The collector's 100 ms throttle is evaluated against this seam.
class FakeClock {
 public:
  explicit FakeClock(std::uint64_t step_ms) : step_(step_ms) {}
  std::function<std::uint64_t()> callable() {
    return [this] {
      now_ += step_;
      return now_;
    };
  }

 private:
  std::uint64_t step_;
  std::uint64_t now_ = 0;
};

void require_sections_equal(const StatisticsSnapshot& a,
                            const StatisticsSnapshot& b) {
  REQUIRE(a.overview.state == b.overview.state);
  REQUIRE(a.overview.data.compressed_bytes == b.overview.data.compressed_bytes);
  REQUIRE(a.overview.data.inflated_bytes == b.overview.data.inflated_bytes);
  REQUIRE(a.overview.data.has_compression_totals ==
          b.overview.data.has_compression_totals);
  REQUIRE(a.chunks.state == b.chunks.state);
  REQUIRE(a.chunks.data.count == b.chunks.data.count);
  REQUIRE(a.chunks.data.data_bytes == b.chunks.data.data_bytes);
  REQUIRE(a.chunks.data.buckets == b.chunks.data.buckets);
  REQUIRE(a.filters.state == b.filters.state);
  REQUIRE(a.filters.data.rows == b.filters.data.rows);
  REQUIRE(a.filters.data.data_bytes == b.filters.data.data_bytes);
  REQUIRE(a.filters.data.invalid_rows == b.filters.data.invalid_rows);
  REQUIRE(a.filters.data.buckets == b.filters.data.buckets);
  REQUIRE(a.blocks.state == b.blocks.state);
  REQUIRE(a.blocks.data.count == b.blocks.data.count);
  REQUIRE(a.blocks.data.compressed_bits == b.blocks.data.compressed_bits);
  REQUIRE(a.blocks.data.output_bytes == b.blocks.data.output_bytes);
  REQUIRE(a.blocks.data.buckets == b.blocks.data.buckets);
  REQUIRE(a.tokens.state == b.tokens.state);
  REQUIRE(a.tokens.data.count == b.tokens.data.count);
  REQUIRE(a.tokens.data.input_bits == b.tokens.data.input_bits);
  REQUIRE(a.tokens.data.output_bytes == b.tokens.data.output_bytes);
  REQUIRE(a.tokens.data.buckets == b.tokens.data.buckets);
  REQUIRE(a.lengths.state == b.lengths.state);
  REQUIRE(a.lengths.data.buckets == b.lengths.data.buckets);
  REQUIRE(a.distances.state == b.distances.state);
  REQUIRE(a.distances.data.buckets == b.distances.data.buckets);
}

std::vector<std::byte> zlib_compress_level0(const std::vector<std::byte>& raw) {
  z_stream strm{};
  if (deflateInit2(&strm, 0, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(raw.data()));
  strm.avail_in = static_cast<uInt>(raw.size());
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  if (rc != Z_STREAM_END) {
    return {};
  }
  out.resize(strm.total_out);
  return out;
}

// Gray8 non-interlaced PNG whose zlib stream is forced to level 0, so every
// filtered byte decodes as exactly one literal token and the token count is
// known without a decoder.
std::vector<std::byte> make_level0_stored_png(
    std::uint32_t w, std::uint32_t h, std::vector<std::byte>* filtered_out) {
  std::vector<std::byte> filtered;
  for (std::uint32_t y = 0; y < h; ++y) {
    filtered.push_back(std::byte{0});  // filter type None
    for (std::uint32_t x = 0; x < w; ++x) {
      filtered.push_back(static_cast<std::byte>(1 + ((x * 3 + y * 7) % 250)));
    }
  }
  if (filtered_out != nullptr) {
    *filtered_out = filtered;
  }

  std::vector<std::byte> bytes(pnga::png_format::kPngSignature.begin(),
                               pnga::png_format::kPngSignature.end());
  const auto push = [&bytes](const char* type,
                             const std::vector<std::byte>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(static_cast<std::byte>((len >> shift) & 0xFFu));
    }
    uLong crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<std::byte>(type[i]));
    }
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
      bytes.insert(bytes.end(), data.begin(), data.end());
    }
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(static_cast<std::byte>((crc >> shift) & 0xFFu));
    }
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = static_cast<std::byte>((w >> 24) & 0xFFu);
  ihdr[1] = static_cast<std::byte>((w >> 16) & 0xFFu);
  ihdr[2] = static_cast<std::byte>((w >> 8) & 0xFFu);
  ihdr[3] = static_cast<std::byte>(w & 0xFFu);
  ihdr[4] = static_cast<std::byte>((h >> 24) & 0xFFu);
  ihdr[5] = static_cast<std::byte>((h >> 16) & 0xFFu);
  ihdr[6] = static_cast<std::byte>((h >> 8) & 0xFFu);
  ihdr[7] = static_cast<std::byte>(h & 0xFFu);
  ihdr[8] = std::byte{8};  // bit depth
  ihdr[9] = std::byte{0};  // color type gray
  push("IHDR", ihdr);
  push("IDAT", zlib_compress_level0(filtered));
  push("IEND", {});
  return bytes;
}

}  // namespace

TEST_CASE("Collector publishes section progress in frozen order",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;
  const auto fixture = make_fixture(make_controlled_fixture(
      ControlledCaseId::kTraceFixedNonoverlap).png_bytes);
  auto request = make_request(fixture);
  request.monotonic_millis = FakeClock(100).callable();

  std::vector<SId> order;
  std::vector<StatisticsSnapshot> published;
  const auto result = collect_document_statistics(
      request, nullptr,
      [&](const StatisticsCollectionResult& r, const StatisticsProgress& p) {
        REQUIRE(r.generation == 7);
        order.push_back(p.section);
        published.push_back(r.snapshot);
      });

  REQUIRE(result.generation == 7);
  // At most 10 progress callbacks per logical second (100 ms throttle, the
  // fake clock advances exactly 100 ms per collector clock call).
  REQUIRE(order.size() <= 10);
  REQUIRE(order.size() >= 5);
  // overview/chunks/filters/blocks always precede tokens.
  const SId expected_front[] = {SId::kOverview, SId::kChunks, SId::kFilters,
                                SId::kBlocks, SId::kTokens};
  for (std::size_t i = 0; i < 5; ++i) {
    REQUIRE(order[i] == expected_front[i]);
  }
  // Every later snapshot preserves the earlier verified rows.
  for (std::size_t i = 1; i < published.size(); ++i) {
    const StatisticsSnapshot& previous = published[i - 1];
    const StatisticsSnapshot& current = published[i];
    if (previous.chunks.state.status == SectionStatus::kReady) {
      REQUIRE(current.chunks.state.status == SectionStatus::kReady);
      REQUIRE(current.chunks.data.count == previous.chunks.data.count);
      REQUIRE(current.chunks.data.data_bytes ==
              previous.chunks.data.data_bytes);
      REQUIRE(current.chunks.data.buckets == previous.chunks.data.buckets);
    }
    if (previous.filters.state.status == SectionStatus::kReady) {
      REQUIRE(current.filters.state.status == SectionStatus::kReady);
      REQUIRE(current.filters.data.rows == previous.filters.data.rows);
      REQUIRE(current.filters.data.data_bytes ==
              previous.filters.data.data_bytes);
      REQUIRE(current.filters.data.buckets == previous.filters.data.buckets);
    }
    if (previous.blocks.state.status == SectionStatus::kReady) {
      REQUIRE(current.blocks.state.status == SectionStatus::kReady);
      REQUIRE(current.blocks.data.count == previous.blocks.data.count);
      REQUIRE(current.blocks.data.compressed_bits ==
              previous.blocks.data.compressed_bits);
      REQUIRE(current.blocks.data.output_bytes ==
              previous.blocks.data.output_bytes);
      REQUIRE(current.blocks.data.buckets == previous.blocks.data.buckets);
    }
  }
  // The last published snapshot is the final result.
  require_sections_equal(published.back(), result.snapshot);
  REQUIRE(result.snapshot.complete());
}

TEST_CASE("Progress is throttled through the injected monotonic clock",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;
  const auto fixture = make_fixture(make_controlled_fixture(
      ControlledCaseId::kTraceFixedNonoverlap).png_bytes);
  auto request = make_request(fixture);
  // 1 ms logical steps: only the unconditional first publish fits inside the
  // 100 ms throttle window for this small document.
  request.monotonic_millis = FakeClock(1).callable();

  std::size_t callbacks = 0;
  collect_document_statistics(request, nullptr,
                              [&](const StatisticsCollectionResult&,
                                  const StatisticsProgress&) { ++callbacks; });
  REQUIRE(callbacks >= 1);
  REQUIRE(callbacks <= 2);
}

TEST_CASE("Collector matches the rich adapter oracle on corpus fixtures",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;
  using pnga::deflate_trace::TokenDecodeResult;

  const ControlledCaseId cases[] = {
      ControlledCaseId::kTraceStoredLiterals,
      ControlledCaseId::kTraceFixedNonoverlap,
      ControlledCaseId::kTraceDynamicOverlapRepeats,
      ControlledCaseId::kTraceMultiblockBfinal,
      ControlledCaseId::kIdatSplitZlibHeader,
      ControlledCaseId::kIdatSplitToken,
      ControlledCaseId::kIdatSplitAdler,
      ControlledCaseId::kErrorTruncatedHeader,
      ControlledCaseId::kErrorTruncatedToken,
      ControlledCaseId::kErrorReservedBtype,
      ControlledCaseId::kErrorInvalidDistance,
      ControlledCaseId::kErrorAdlerMismatch,
      ControlledCaseId::kErrorCrcMismatch,
  };
  for (const auto id : cases) {
    const auto fixture = make_fixture(make_controlled_fixture(id).png_bytes);
    CAPTURE(std::string("case id ") + std::to_string(static_cast<int>(id)));
    const auto request = make_request(fixture, 3);
    const auto result = collect_document_statistics(request, nullptr, {});
    REQUIRE(result.generation == 3);

    // Oracle: the frozen rich path through the WP-602A adapter.
    VirtualIDATStream stream(fixture.chunks);
    OracleIdatSource logical(stream, *fixture.source);
    const auto blocks = index_blocks(logical, 1u << 22);
    const TokenDecodeResult tokens = decode_stored_and_fixed(logical, 1u << 22);
    const pnga::analysis_engine::StatisticsSources sources{
        &fixture.chunks, fixture.stages.get(), &blocks, &tokens};
    const auto oracle = collect_statistics(sources, StatisticsLimits{});

    // Chunks/filters/blocks/overview follow the frozen adapter mapping.
    REQUIRE(result.snapshot.overview.state == oracle.overview.state);
    REQUIRE(result.snapshot.overview.data.compressed_bytes ==
            oracle.overview.data.compressed_bytes);
    REQUIRE(result.snapshot.overview.data.inflated_bytes ==
            oracle.overview.data.inflated_bytes);
    REQUIRE(result.snapshot.chunks.state == oracle.chunks.state);
    REQUIRE(result.snapshot.chunks.data.count == oracle.chunks.data.count);
    REQUIRE(result.snapshot.chunks.data.buckets == oracle.chunks.data.buckets);
    REQUIRE(result.snapshot.filters.state == oracle.filters.state);
    REQUIRE(result.snapshot.filters.data.rows == oracle.filters.data.rows);
    REQUIRE(result.snapshot.filters.data.buckets ==
            oracle.filters.data.buckets);
    REQUIRE(result.snapshot.blocks.state == oracle.blocks.state);
    REQUIRE(result.snapshot.blocks.data.count == oracle.blocks.data.count);
    REQUIRE(result.snapshot.blocks.data.buckets == oracle.blocks.data.buckets);

    if (tokens.success) {
      require_sections_equal(result.snapshot, oracle);
    } else {
      // The streamed collector keeps the validated token prefix that the
      // rich-list adapter discards: the malformed stream's verified rows.
      REQUIRE(result.snapshot.tokens.state.status ==
              SectionStatus::kInvalidInput);
      REQUIRE(result.snapshot.tokens.state.scope ==
              SectionScope::kVerifiedPrefix);
      REQUIRE(result.snapshot.tokens.data.count == tokens.tokens.size());
      std::uint64_t input_bits = 0;
      std::uint64_t output_bytes = 0;
      std::uint64_t literals = 0;
      std::uint64_t matches = 0;
      std::uint64_t end_of_blocks = 0;
      std::vector<pnga::statistics::ValueBucket> lengths;
      std::vector<pnga::statistics::ValueBucket> distances;
      const auto add_to = [](std::vector<pnga::statistics::ValueBucket>* buckets,
                             std::uint64_t value) {
        auto it = std::lower_bound(
            buckets->begin(), buckets->end(), value,
            [](const pnga::statistics::ValueBucket& bucket,
               std::uint64_t wanted) { return bucket.value < wanted; });
        if (it != buckets->end() && it->value == value) {
          ++it->count;
          return;
        }
        buckets->insert(it, pnga::statistics::ValueBucket{value, 1});
      };
      for (const auto& token : tokens.tokens) {
        input_bits += token.input_bit_end - token.input_bit_begin;
        output_bytes += token.output_end - token.output_begin;
        switch (token.kind) {
          case pnga::deflate_trace::TokenKind::kLiteral:
            ++literals;
            break;
          case pnga::deflate_trace::TokenKind::kLengthDistance:
            ++matches;
            add_to(&lengths, token.length);
            add_to(&distances, token.distance);
            break;
          case pnga::deflate_trace::TokenKind::kEndOfBlock:
            ++end_of_blocks;
            break;
        }
      }
      REQUIRE(result.snapshot.tokens.data.input_bits == input_bits);
      REQUIRE(result.snapshot.tokens.data.output_bytes == output_bytes);
      REQUIRE(result.snapshot.tokens.data.buckets[0].count == literals);
      REQUIRE(result.snapshot.tokens.data.buckets[1].count == matches);
      REQUIRE(result.snapshot.tokens.data.buckets[2].count == end_of_blocks);
      REQUIRE(result.snapshot.lengths.state.status ==
              SectionStatus::kInvalidInput);
      REQUIRE(result.snapshot.lengths.state.scope ==
              SectionScope::kVerifiedPrefix);
      REQUIRE(result.snapshot.lengths.data.buckets == lengths);
      REQUIRE(result.snapshot.distances.state.status ==
              SectionStatus::kInvalidInput);
      REQUIRE(result.snapshot.distances.state.scope ==
              SectionScope::kVerifiedPrefix);
      REQUIRE(result.snapshot.distances.data.buckets == distances);
    }
  }
}

TEST_CASE("Missing or failed StageSet affects only the filters section",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;
  const auto fixture = make_fixture(make_controlled_fixture(
      ControlledCaseId::kTraceStoredLiterals).png_bytes);

  {
    auto request = make_request(fixture);
    request.stages = nullptr;
    const auto result = collect_document_statistics(request, nullptr, {});
    REQUIRE(result.snapshot.filters.state.status ==
            SectionStatus::kUnavailable);
    REQUIRE(result.snapshot.filters.state.scope == SectionScope::kNone);
    REQUIRE(result.snapshot.chunks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.blocks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.tokens.state.status == SectionStatus::kReady);
    REQUIRE_FALSE(result.snapshot.complete());
  }
  {
    auto failed = std::make_shared<StageSet>();
    failed->success = false;
    failed->error = "stage analysis failed";
    auto request = make_request(fixture);
    request.stages = failed;
    const auto result = collect_document_statistics(request, nullptr, {});
    REQUIRE(result.snapshot.filters.state.status ==
            SectionStatus::kInvalidInput);
    REQUIRE(result.snapshot.filters.state.error ==
            "cannot collect statistics from failed stage analysis");
    REQUIRE(result.snapshot.chunks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.blocks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.tokens.state.status == SectionStatus::kReady);
  }
}

TEST_CASE("Cancellation marks the matching section and dependents",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;

  // (a) Cancel before the fingerprint: nothing is collected.
  {
    const auto fixture = make_fixture(make_controlled_fixture(
        ControlledCaseId::kTraceStoredLiterals).png_bytes);
    auto request = make_request(fixture);
    CancellationToken token;
    token.request_cancel();
    const auto result = collect_document_statistics(request, &token, {});
    REQUIRE(result.snapshot.overview.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.chunks.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.filters.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.blocks.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.tokens.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.lengths.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.distances.state.status ==
            SectionStatus::kCancelled);
    REQUIRE(result.snapshot.overview.state.scope == SectionScope::kNone);
    REQUIRE(result.document.file_size == 0);
    REQUIRE(result.document.fingerprint.empty());
  }

  // (b) Cancel after the filters publish: blocks and its dependents stop
  // cancelled while the completed sections remain ready. The fake clock
  // makes every fast-section publish fire so the callback lands the cancel
  // deterministically.
  {
    const auto fixture = make_fixture(make_controlled_fixture(
        ControlledCaseId::kTraceFixedNonoverlap).png_bytes);
    auto request = make_request(fixture);
    request.monotonic_millis = FakeClock(100).callable();
    CancellationToken token;
    const auto result = collect_document_statistics(
        request, &token,
        [&](const StatisticsCollectionResult&, const StatisticsProgress& p) {
          if (p.section == SId::kFilters) {
            token.request_cancel();
          }
        });
    REQUIRE(result.snapshot.chunks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.chunks.state.complete);
    REQUIRE(result.snapshot.filters.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.blocks.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.tokens.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.lengths.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.distances.state.status ==
            SectionStatus::kCancelled);
    // The verified chunk evidence keeps the overview partial.
    REQUIRE(result.snapshot.overview.state.status == SectionStatus::kPartial);
    REQUIRE(result.snapshot.overview.state.scope ==
            SectionScope::kVerifiedPrefix);
    REQUIRE_FALSE(result.snapshot.overview.data.has_compression_totals);
    // The fingerprint completed before the cancellation.
    REQUIRE(result.document.file_size == fixture.bytes.size());
  }

  // (c) Cancel during the token scan: the streamed prefix survives as a
  // verified prefix and completed sections remain ready. The level-0 stored
  // fixture has 16512 literal tokens, so the cancel lands at a known fact.
  {
    std::vector<std::byte> filtered;
    const auto fixture =
        make_fixture(make_level0_stored_png(128, 128, &filtered));
    REQUIRE(filtered.size() == 128u * 129u);
    auto request = make_request(fixture);
    request.monotonic_millis = FakeClock(100).callable();
    CancellationToken token;
    const auto result = collect_document_statistics(
        request, &token,
        [&](const StatisticsCollectionResult&, const StatisticsProgress& p) {
          if (p.section == SId::kTokens) {
            token.request_cancel();
          }
        });
    REQUIRE(result.snapshot.chunks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.filters.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.blocks.state.status == SectionStatus::kReady);
    REQUIRE(result.snapshot.tokens.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.tokens.state.scope ==
            SectionScope::kVerifiedPrefix);
    // The cancel publishes token progress at fact 256; the scan's own
    // periodic check (every 256 tokens) fires immediately after, so exactly
    // 256 facts are aggregated.
    REQUIRE(result.snapshot.tokens.data.count == 256);
    REQUIRE(result.snapshot.lengths.state.status == SectionStatus::kCancelled);
    REQUIRE(result.snapshot.lengths.state.scope ==
            SectionScope::kVerifiedPrefix);
    REQUIRE(result.snapshot.distances.state.scope ==
            SectionScope::kVerifiedPrefix);
    REQUIRE(result.snapshot.lengths.data.buckets.empty());
    REQUIRE(result.snapshot.distances.data.buckets.empty());
    // Completed sections keep their ready totals.
    REQUIRE(result.snapshot.overview.state.status == SectionStatus::kReady);
  }
}

TEST_CASE("Collector rejects budgets beyond the frozen caps",
          "[analysis-engine][wp602c]") {
  using pnga_test::wp607c::ControlledCaseId;
  using pnga_test::wp607c::make_controlled_fixture;
  const auto fixture = make_fixture(make_controlled_fixture(
      ControlledCaseId::kTraceStoredLiterals).png_bytes);

  {
    auto request = make_request(fixture);
    request.max_working_bytes = (64ull << 20) + 1;
    const auto result = collect_document_statistics(request, nullptr, {});
    REQUIRE(result.snapshot.chunks.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.tokens.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.overview.state.scope == SectionScope::kNone);
    REQUIRE(result.document.file_size == 0);
  }
  {
    auto request = make_request(fixture);
    request.limits.max_samples = 0;
    const auto result = collect_document_statistics(request, nullptr, {});
    REQUIRE(result.snapshot.chunks.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.filters.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.blocks.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.tokens.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.lengths.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.distances.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.snapshot.overview.state.status ==
            SectionStatus::kBudgetExceeded);
    REQUIRE(result.document.file_size == 0);
  }
}

TEST_CASE("Collector aggregates a stored stream exactly like hand-built input",
          "[analysis-engine][wp602c]") {
  std::vector<std::byte> filtered;
  const auto fixture = make_fixture(make_level0_stored_png(96, 64, &filtered));
  const std::uint64_t row_bytes = 96;  // gray8: one byte per pixel
  REQUIRE(filtered.size() == 64u * (row_bytes + 1));

  auto request = make_request(fixture);
  const auto result = collect_document_statistics(request, nullptr, {});
  REQUIRE(result.snapshot.complete());

  // Hand-built scalar input: chunk samples from the index, all-none filter
  // rows, one 8-bit literal per filtered byte, one EOB per stored block.
  // The chunk type strings must outlive the samples' string_views, so the
  // storage is reserved up front: pushing more strings never reallocates.
  std::vector<std::string> type_storage;
  type_storage.reserve(fixture.chunks.chunks.size());
  std::vector<ChunkSample> chunk_samples;
  std::uint64_t idat_bytes = 0;
  for (const auto& chunk : fixture.chunks.chunks) {
    type_storage.push_back(chunk.text());
    chunk_samples.push_back(
        ChunkSample{type_storage.back(), chunk.data_length});
    if (type_storage.back() == "IDAT") {
      idat_bytes += chunk.data_length;
    }
  }
  std::vector<FilterSample> filter_samples;
  for (std::uint32_t row = 0; row < 64; ++row) {
    filter_samples.push_back(FilterSample{0, row_bytes});
  }
  VirtualIDATStream stream(fixture.chunks);
  OracleIdatSource logical(stream, *fixture.source);
  const auto blocks = index_blocks(logical, 1u << 22);
  REQUIRE(blocks.success);
  std::vector<BlockSample> block_samples;
  for (const auto& block : blocks.blocks) {
    BlockKind kind = BlockKind::kStored;
    switch (block.type) {
      case pnga::deflate_index::BlockType::kStored:
        kind = BlockKind::kStored;
        break;
      case pnga::deflate_index::BlockType::kFixed:
        kind = BlockKind::kFixed;
        break;
      case pnga::deflate_index::BlockType::kDynamic:
        kind = BlockKind::kDynamic;
        break;
    }
    block_samples.push_back(BlockSample{kind,
                                        block.input_bit_end -
                                            block.input_bit_begin,
                                        block.output_end -
                                            block.output_begin});
  }
  std::vector<TokenSample> token_samples;
  for (std::size_t i = 0; i < filtered.size(); ++i) {
    token_samples.push_back(TokenSample{TokenKind::kLiteral, 8, 1, 0, 0});
  }
  for (std::size_t i = 0; i < blocks.blocks.size(); ++i) {
    token_samples.push_back(TokenSample{TokenKind::kEndOfBlock, 0, 0, 0, 0});
  }

  StatisticsInput input{chunk_samples,
                        filter_samples,
                        block_samples,
                        token_samples,
                        idat_bytes,
                        filtered.size(),
                        true};
  const auto expected = pnga::statistics::collect(input, StatisticsLimits{});
  require_sections_equal(result.snapshot, expected);

  // Exact streamed token totals, aggregated without any event list.
  REQUIRE(result.snapshot.tokens.data.count ==
          filtered.size() + blocks.blocks.size());
  REQUIRE(result.snapshot.tokens.data.buckets[0].count == filtered.size());
  REQUIRE(result.snapshot.tokens.data.buckets[2].count ==
          blocks.blocks.size());
  REQUIRE(result.snapshot.tokens.data.input_bits == filtered.size() * 8);
  REQUIRE(result.snapshot.tokens.data.output_bytes == filtered.size());
  REQUIRE(result.snapshot.lengths.data.buckets.empty());
  REQUIRE(result.snapshot.distances.data.buckets.empty());
}
