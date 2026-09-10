// WP-APNG-INSPECT T05: independent pnga.frame-statistics export schema.
// The new schema reuses the shared section encoding; the static v1 schema
// and its goldens are untouched. These tests pin the envelope field order,
// the fixed CSV columns, the ratio availability rule and the identity
// validation with self-contained golden strings.

#include <pnga/statistics/frame_statistics.h>

#include <pnga/statistics/statistics.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using pnga::statistics::DocumentIdentity;
using pnga::statistics::FrameStatistics;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsAccumulator;
using pnga::statistics::serialize_frame_statistics_csv;
using pnga::statistics::serialize_frame_statistics_json;

namespace {

DocumentIdentity test_identity() {
  return DocumentIdentity{1234, "fnv1a64-v1:0123456789abcdef"};
}

FrameStatistics incomplete_frame() {
  FrameStatistics frame;
  frame.document = test_identity();
  frame.identity = pnga::trace_model::AnimationFrame{1};
  frame.width = 2;
  frame.height = 3;
  frame.payload_bytes = 21;
  frame.chunk_overhead_bytes = 54;
  frame.inflated_bytes = 27;
  return frame;
}

// A complete snapshot via the shared accumulator (one chunk, one filter,
// one block, one literal token + EOB).
pnga::statistics::StatisticsSnapshot complete_snapshot() {
  StatisticsAccumulator accumulator;
  REQUIRE(accumulator.add(pnga::statistics::ChunkSample{"fcTL", 26}));
  REQUIRE(accumulator.add(pnga::statistics::FilterSample{0, 8}));
  REQUIRE(accumulator.add(pnga::statistics::BlockSample{
      pnga::statistics::BlockKind::kDynamic, 57, 9}));
  REQUIRE(accumulator.add(pnga::statistics::TokenSample{
      pnga::statistics::TokenKind::kLiteral, 8, 1, 0, 0}));
  accumulator.finish(pnga::statistics::StatisticsSectionId::kChunks,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  accumulator.finish(pnga::statistics::StatisticsSectionId::kFilters,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  accumulator.finish(pnga::statistics::StatisticsSectionId::kBlocks,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  accumulator.finish(pnga::statistics::StatisticsSectionId::kTokens,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  accumulator.finish(pnga::statistics::StatisticsSectionId::kLengths,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  accumulator.finish(pnga::statistics::StatisticsSectionId::kDistances,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  REQUIRE(accumulator.set_compression_totals(21, 27));
  accumulator.finish(pnga::statistics::StatisticsSectionId::kOverview,
                     SectionStatus::kReady, true, SectionScope::kWholeDocument,
                     "");
  return accumulator.snapshot();
}

void require_byte_contract(const std::string& bytes) {
  REQUIRE_FALSE(bytes.empty());
  REQUIRE(bytes.back() == '\n');
  REQUIRE(bytes.find("\r") == std::string::npos);
  REQUIRE(bytes.substr(0, 3) != "\xef\xbb\xbf");  // no UTF-8 BOM
}

}  // namespace

TEST_CASE("Frame statistics JSON carries the frozen envelope", "[apng-inspect]") {
  pnga::statistics::FrameStatistics frame = incomplete_frame();
  const auto json = serialize_frame_statistics_json(frame);
  REQUIRE(json.success);
  require_byte_contract(json.bytes);
  REQUIRE(json.bytes.find("\"schema\": \"pnga.frame-statistics\"") !=
          std::string::npos);
  REQUIRE(json.bytes.find("\"schema_version\": 1") != std::string::npos);
  // Field order: schema, schema_version, document, identity, geometry,
  // bytes, sections.
  REQUIRE(json.bytes.find("\"schema\"") < json.bytes.find("\"schema_version\""));
  REQUIRE(json.bytes.find("\"schema_version\"") <
          json.bytes.find("\"document\""));
  REQUIRE(json.bytes.find("\"document\"") < json.bytes.find("\"identity\""));
  REQUIRE(json.bytes.find("\"identity\"") < json.bytes.find("\"geometry\""));
  REQUIRE(json.bytes.find("\"geometry\"") < json.bytes.find("\"bytes\""));
  REQUIRE(json.bytes.find("\"bytes\"") < json.bytes.find("\"sections\""));
  REQUIRE(json.bytes.find("\"kind\": \"animation_frame\"") !=
          std::string::npos);
  REQUIRE(json.bytes.find("\"index\": 1") != std::string::npos);
  REQUIRE(json.bytes.find("\"width\": 2") != std::string::npos);
  REQUIRE(json.bytes.find("\"payload\": 21") != std::string::npos);
  REQUIRE(json.bytes.find("\"chunk_overhead\": 54") != std::string::npos);
  REQUIRE(json.bytes.find("\"inflated\": 27") != std::string::npos);
  // Incomplete snapshot: the ratio is not computable and stays null.
  REQUIRE(json.bytes.find("\"ratio\": null") != std::string::npos);
}

TEST_CASE("Frame statistics JSON emits the ratio only when complete",
          "[apng-inspect]") {
  FrameStatistics frame = incomplete_frame();
  frame.snapshot = complete_snapshot();
  REQUIRE(frame.snapshot.complete());
  const auto json = serialize_frame_statistics_json(frame);
  REQUIRE(json.success);
  REQUIRE(json.bytes.find("\"ratio\": \"21/27\"") != std::string::npos);

  // Zero denominator: the ratio is unavailable even though the snapshot is
  // complete.
  frame.inflated_bytes = 0;
  frame.snapshot = complete_snapshot();
  const auto zero = serialize_frame_statistics_json(frame);
  REQUIRE(zero.success);
  REQUIRE(zero.bytes.find("\"ratio\": null") != std::string::npos);
}

TEST_CASE("Frame statistics CSV uses the frozen columns", "[apng-inspect]") {
  pnga::statistics::FrameStatistics frame = incomplete_frame();
  frame.snapshot = complete_snapshot();
  const auto csv = serialize_frame_statistics_csv(frame);
  REQUIRE(csv.success);
  require_byte_contract(csv.bytes);
  REQUIRE(csv.bytes.rfind(
              "schema,schema_version,frame_index,section,metric,key,value,"
              "unit\n",
              0) == 0);
  REQUIRE(csv.bytes.find("pnga.frame-statistics,1,1,frame,kind,,"
                         "animation_frame,\n") != std::string::npos);
  REQUIRE(csv.bytes.find("pnga.frame-statistics,1,1,frame,payload,,21,"
                         "bytes\n") != std::string::npos);
  REQUIRE(csv.bytes.find("pnga.frame-statistics,1,1,frame,chunk_overhead,,54,"
                         "bytes\n") != std::string::npos);
  REQUIRE(csv.bytes.find("pnga.frame-statistics,1,1,frame,ratio,,21/27,\n") !=
          std::string::npos);
  // Section rows reuse the shared encoding with the frame leading cells.
  REQUIRE(csv.bytes.find("pnga.frame-statistics,1,1,overview,status,,ready,\n") !=
          std::string::npos);
}

TEST_CASE("Frame statistics serializers reject static identities",
          "[apng-inspect]") {
  FrameStatistics frame = incomplete_frame();
  frame.identity = pnga::trace_model::StaticImage{};
  REQUIRE_FALSE(serialize_frame_statistics_json(frame).success);
  REQUIRE_FALSE(serialize_frame_statistics_csv(frame).success);

  frame.document.fingerprint = "not-a-fingerprint";
  frame.identity = pnga::trace_model::AnimationFrame{0};
  const auto json = serialize_frame_statistics_json(frame);
  REQUIRE_FALSE(json.success);
  REQUIRE(json.error == "invalid document fingerprint");
}
