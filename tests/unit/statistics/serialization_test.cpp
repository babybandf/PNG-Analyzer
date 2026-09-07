#include <pnga/statistics/serialization.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <pnga/statistics/statistics.h>

namespace {

#ifndef PNGA_STATISTICS_GOLDEN_DIR
#error "PNGA_STATISTICS_GOLDEN_DIR must be defined for serializer goldens"
#endif

using pnga::statistics::BlockBucket;
using pnga::statistics::ChunkBucket;
using pnga::statistics::DocumentIdentity;
using pnga::statistics::FilterBucket;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionState;
using pnga::statistics::SectionStatus;
using pnga::statistics::SerializationResult;
using pnga::statistics::StatisticsSnapshot;
using pnga::statistics::TokenBucket;
using pnga::statistics::ValueBucket;

std::string read_golden(const std::string& name) {
  const std::filesystem::path path =
      std::filesystem::path(PNGA_STATISTICS_GOLDEN_DIR) / name;
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

DocumentIdentity valid_identity() {
  return DocumentIdentity{1234, "fnv1a64-v1:0123456789abcdef"};
}

DocumentIdentity partial_identity() {
  return DocumentIdentity{68719476736ULL, "fnv1a64-v1:deadbeefdeadbeef"};
}

void mark_ready(SectionState& state) {
  state.status = SectionStatus::kReady;
  state.complete = true;
  state.scope = SectionScope::kWholeDocument;
}

StatisticsSnapshot ready_snapshot() {
  StatisticsSnapshot snapshot;
  mark_ready(snapshot.overview.state);
  snapshot.overview.data.compressed_bytes = 65;
  snapshot.overview.data.inflated_bytes = 120;
  snapshot.overview.data.has_compression_totals = true;

  mark_ready(snapshot.chunks.state);
  snapshot.chunks.data.count = 3;
  snapshot.chunks.data.data_bytes = 133;
  snapshot.chunks.data.buckets = std::vector<ChunkBucket>{
      {"IDAT", 2, 120}, {"IHDR", 1, 13}};

  mark_ready(snapshot.filters.state);
  snapshot.filters.data.rows = 3;
  snapshot.filters.data.data_bytes = 30;
  snapshot.filters.data.invalid_rows = 0;
  snapshot.filters.data.buckets = std::vector<FilterBucket>{
      {2, 20}, {0, 0}, {0, 0}, {0, 0}, {1, 10}};

  mark_ready(snapshot.blocks.state);
  snapshot.blocks.data.count = 2;
  snapshot.blocks.data.compressed_bits = 30;
  snapshot.blocks.data.output_bytes = 120;
  snapshot.blocks.data.buckets = std::vector<BlockBucket>{
      {0, 0, 0}, {1, 10, 20}, {1, 20, 100}};

  mark_ready(snapshot.tokens.state);
  snapshot.tokens.data.count = 4;
  snapshot.tokens.data.input_bits = 40;
  snapshot.tokens.data.output_bytes = 10;
  snapshot.tokens.data.buckets = std::vector<TokenBucket>{
      {1, 8, 1}, {2, 25, 9}, {1, 7, 0}};

  mark_ready(snapshot.lengths.state);
  snapshot.lengths.data.buckets = std::vector<ValueBucket>{{5, 2}};
  mark_ready(snapshot.distances.state);
  snapshot.distances.data.buckets = std::vector<ValueBucket>{{3, 2}};
  return snapshot;
}

// Mixed ready/partial/cancelled/budget/invalid/overflow/unavailable section
// states, including comma, quote and LF in a synthetic error and a synthetic
// bucket key. Together with the ready golden this covers every section status
// of the frozen vocabulary.
StatisticsSnapshot partial_snapshot() {
  StatisticsSnapshot snapshot;

  snapshot.chunks.state.status = SectionStatus::kCancelled;
  snapshot.chunks.state.complete = false;
  snapshot.chunks.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.chunks.state.error = "statistics collection cancelled";
  snapshot.chunks.data.count = 3;
  snapshot.chunks.data.data_bytes = 115;
  snapshot.chunks.data.buckets = std::vector<ChunkBucket>{
      {"IDAT", 2, 110}, {"a\"b,c", 1, 5}};

  snapshot.filters.state.status = SectionStatus::kInvalidInput;
  snapshot.filters.state.complete = false;
  snapshot.filters.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.filters.state.error =
      "filtered scanline range is outside its backing buffer";
  snapshot.filters.data.rows = 1;
  snapshot.filters.data.data_bytes = 8;
  snapshot.filters.data.invalid_rows = 0;
  snapshot.filters.data.buckets = std::vector<FilterBucket>{
      {1, 8}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

  snapshot.blocks.state.status = SectionStatus::kPartial;
  snapshot.blocks.state.complete = false;
  snapshot.blocks.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.blocks.data.count = 1;
  snapshot.blocks.data.compressed_bits = 10;
  snapshot.blocks.data.output_bytes = 20;
  snapshot.blocks.data.buckets = std::vector<BlockBucket>{
      {1, 10, 20}, {0, 0, 0}, {0, 0, 0}};

  snapshot.tokens.state.status = SectionStatus::kBudgetExceeded;
  snapshot.tokens.state.complete = false;
  snapshot.tokens.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.tokens.state.error =
      "budget hit: \"length histogram\" exceeded\nsecond line";
  snapshot.tokens.data.count = 3;
  snapshot.tokens.data.input_bits = 25;
  snapshot.tokens.data.output_bytes = 4;
  snapshot.tokens.data.buckets = std::vector<TokenBucket>{
      {2, 17, 2}, {1, 8, 2}, {0, 0, 0}};

  snapshot.lengths.state.status = SectionStatus::kBudgetExceeded;
  snapshot.lengths.state.complete = false;
  snapshot.lengths.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.lengths.state.error =
      "budget hit: \"length histogram\" exceeded\nsecond line";
  snapshot.lengths.data.buckets = std::vector<ValueBucket>{{3, 1}};

  snapshot.distances.state.status = SectionStatus::kOverflow;
  snapshot.distances.state.complete = false;
  snapshot.distances.state.scope = SectionScope::kVerifiedPrefix;
  snapshot.distances.state.error = "statistics count overflow";
  snapshot.distances.data.buckets = std::vector<ValueBucket>{{1, 1}};
  return snapshot;
}

// Switches the global C locale and always restores the previous one.
class LocaleGuard {
 public:
  explicit LocaleGuard(const char* name)
      : previous_(std::setlocale(LC_ALL, nullptr)) {
    applied_ = std::setlocale(LC_ALL, name) != nullptr;
  }
  ~LocaleGuard() { std::setlocale(LC_ALL, previous_.c_str()); }
  LocaleGuard(const LocaleGuard&) = delete;
  LocaleGuard& operator=(const LocaleGuard&) = delete;
  bool applied() const { return applied_; }

 private:
  std::string previous_;
  bool applied_ = false;
};

// Report bytes must be UTF-8 without BOM, LF only, with exactly one trailing
// LF, no extra final blank line and no absolute path.
void assert_report_bytes(const std::string& bytes) {
  REQUIRE_FALSE(bytes.empty());
  REQUIRE(bytes.back() == '\n');
  REQUIRE((bytes.size() < 2 || bytes[bytes.size() - 2] != '\n'));
  REQUIRE(bytes.find('\r') == std::string::npos);
  REQUIRE(bytes.substr(0, 3) != std::string("\xEF\xBB\xBF"));
  REQUIRE(bytes.find('/') == std::string::npos);
}

}  // namespace

TEST_CASE("Ready statistics serialize byte-identically across locales",
          "[statistics][wp602d]") {
  const auto document = valid_identity();
  const auto snapshot = ready_snapshot();
  REQUIRE(snapshot.complete());

  const SerializationResult json =
      pnga::statistics::serialize_statistics_json(document, snapshot);
  const SerializationResult csv =
      pnga::statistics::serialize_statistics_csv(document, snapshot);
  REQUIRE(json.success);
  REQUIRE(csv.success);
  assert_report_bytes(json.bytes);
  assert_report_bytes(csv.bytes);

  // C locale output must match the committed goldens byte for byte.
  {
    LocaleGuard guard("C");
    REQUIRE(pnga::statistics::serialize_statistics_json(document, snapshot)
                .bytes == read_golden("ready-v1.json"));
    REQUIRE(pnga::statistics::serialize_statistics_csv(document, snapshot)
                .bytes == read_golden("ready-v1.csv"));
  }
  // C.UTF-8 when present plus every installed non-English candidate locale.
  for (const char* name : {"C.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
    LocaleGuard guard(name);
    if (!guard.applied()) {
      continue;
    }
    REQUIRE(pnga::statistics::serialize_statistics_json(document, snapshot)
                .bytes == json.bytes);
    REQUIRE(pnga::statistics::serialize_statistics_csv(document, snapshot)
                .bytes == csv.bytes);
  }
}

TEST_CASE("Partial statistics serialize byte-identically across locales",
          "[statistics][wp602d]") {
  const auto document = partial_identity();
  const auto snapshot = partial_snapshot();
  REQUIRE_FALSE(snapshot.complete());

  const SerializationResult json =
      pnga::statistics::serialize_statistics_json(document, snapshot);
  const SerializationResult csv =
      pnga::statistics::serialize_statistics_csv(document, snapshot);
  REQUIRE(json.success);
  REQUIRE(csv.success);
  assert_report_bytes(json.bytes);
  assert_report_bytes(csv.bytes);

  {
    LocaleGuard guard("C");
    REQUIRE(pnga::statistics::serialize_statistics_json(document, snapshot)
                .bytes == read_golden("partial-v1.json"));
    REQUIRE(pnga::statistics::serialize_statistics_csv(document, snapshot)
                .bytes == read_golden("partial-v1.csv"));
  }
  for (const char* name : {"C.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
    LocaleGuard guard(name);
    if (!guard.applied()) {
      continue;
    }
    REQUIRE(pnga::statistics::serialize_statistics_json(document, snapshot)
                .bytes == json.bytes);
    REQUIRE(pnga::statistics::serialize_statistics_csv(document, snapshot)
                .bytes == csv.bytes);
  }
}

TEST_CASE("Serializers escape comma, quote and newline", "[statistics][wp602d]") {
  const auto snapshot = partial_snapshot();
  const auto document = partial_identity();

  const auto json =
      pnga::statistics::serialize_statistics_json(document, snapshot);
  const auto csv =
      pnga::statistics::serialize_statistics_csv(document, snapshot);
  REQUIRE(json.success);
  REQUIRE(csv.success);

  // JSON escaping: quotes and LF never appear raw inside strings.
  REQUIRE(json.bytes.find("\"error\": \"budget hit: \\\"length histogram\\\" "
                          "exceeded\\nsecond line\"") != std::string::npos);
  REQUIRE(json.bytes.find("{\"type\": \"a\\\"b,c\", \"count\": 1, "
                          "\"data_bytes\": 5}") != std::string::npos);
  // RFC 4180: quoted fields double internal quotes and may contain LF.
  REQUIRE(csv.bytes.find("1,chunks,bucket.count,\"a\"\"b,c\",1,chunks\n") !=
          std::string::npos);
  REQUIRE(csv.bytes.find("1,tokens,error,,\"budget hit: \"\"length "
                         "histogram\"\" exceeded\nsecond line\",\n") !=
          std::string::npos);
}

TEST_CASE("Unavailable values stay distinct from zero",
          "[statistics][wp602d]") {
  const auto document = valid_identity();

  SECTION("all-unavailable snapshot") {
    const StatisticsSnapshot snapshot;
    const auto json =
        pnga::statistics::serialize_statistics_json(document, snapshot);
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE(json.success);
    REQUIRE(csv.success);
    // JSON marks missing numerics with null, never with a zero value.
    REQUIRE(json.bytes.find("\"chunks\": {\n      \"status\": \"unavailable\",\n"
                            "      \"complete\": false,\n"
                            "      \"scope\": \"none\",\n"
                            "      \"count\": null,\n"
                            "      \"data_bytes\": null,\n"
                            "      \"buckets\": []\n    }") !=
            std::string::npos);
    // CSV emits the metric rows with an empty value field and no buckets.
    REQUIRE(csv.bytes.find("1,chunks,status,,unavailable,\n") !=
            std::string::npos);
    REQUIRE(csv.bytes.find("1,chunks,count,,,chunks\n") != std::string::npos);
    REQUIRE(csv.bytes.find("1,chunks,bucket.count,") == std::string::npos);
    REQUIRE(csv.bytes.find("1,overview,compressed_bytes,,,bytes\n") !=
            std::string::npos);
  }

  SECTION("ready section with zero totals") {
    auto snapshot = ready_snapshot();
    snapshot.chunks.data.count = 0;
    snapshot.chunks.data.data_bytes = 0;
    snapshot.chunks.data.buckets.clear();
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE(csv.success);
    REQUIRE(csv.bytes.find("1,chunks,status,,ready,\n") != std::string::npos);
    REQUIRE(csv.bytes.find("1,chunks,count,,0,chunks\n") != std::string::npos);
  }
}

TEST_CASE("CSV keeps the LF-only byte contract on untrusted fields",
          "[statistics][wp602d]") {
  const auto document = valid_identity();

  SECTION("carriage return in a bucket key is rejected") {
    auto snapshot = ready_snapshot();
    snapshot.chunks.data.buckets[0].type = "ID\rAT";
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE_FALSE(csv.success);
    REQUIRE(csv.error == "statistics csv must not contain carriage returns");
    REQUIRE(csv.bytes.empty());
  }
  SECTION("carriage return in an error string is rejected") {
    auto snapshot = ready_snapshot();
    snapshot.blocks.state.error = "stopped\r\nat line two";
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE_FALSE(csv.success);
    REQUIRE(csv.error == "statistics csv must not contain carriage returns");
    // Deterministic: a repeated serialization fails identically.
    const auto again =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE_FALSE(again.success);
    REQUIRE(again.error == csv.error);
  }
  SECTION("embedded LF stays legal inside quoted fields") {
    auto snapshot = ready_snapshot();
    snapshot.blocks.state.error = "stopped\nat line two";
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE(csv.success);
    REQUIRE(csv.bytes.find("1,blocks,error,,\"stopped\nat line two\",\n") !=
            std::string::npos);
    assert_report_bytes(csv.bytes);
  }
  SECTION("JSON escapes the same untrusted fields losslessly") {
    auto snapshot = ready_snapshot();
    snapshot.blocks.state.error = "stopped\r\nat line two";
    snapshot.chunks.data.buckets[0].type = "ID\rAT";
    const auto json =
        pnga::statistics::serialize_statistics_json(document, snapshot);
    REQUIRE(json.success);
    REQUIRE(json.bytes.find("\"error\": \"stopped\\r\\nat line two\"") !=
            std::string::npos);
    REQUIRE(json.bytes.find("{\"type\": \"ID\\rAT\"") != std::string::npos);
    assert_report_bytes(json.bytes);
  }
}

TEST_CASE("Serializers validate identity and section state",
          "[statistics][wp602d]") {
  SECTION("fingerprint prefix") {
    auto document = valid_identity();
    document.fingerprint = "sha256:0123456789abcdef";
    const auto json =
        pnga::statistics::serialize_statistics_json(document, ready_snapshot());
    REQUIRE_FALSE(json.success);
    REQUIRE(json.error == "invalid document fingerprint");
    REQUIRE(json.bytes.empty());
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, ready_snapshot());
    REQUIRE_FALSE(csv.success);
    REQUIRE(csv.error == "invalid document fingerprint");
  }
  SECTION("fingerprint hex charset and length") {
    auto document = valid_identity();
    document.fingerprint = "fnv1a64-v1:0123456789ABCDEF";
    REQUIRE_FALSE(
        pnga::statistics::serialize_statistics_json(document, ready_snapshot())
            .success);
    document.fingerprint = "fnv1a64-v1:0123456789abcde";
    REQUIRE_FALSE(
        pnga::statistics::serialize_statistics_csv(document, ready_snapshot())
            .success);
    document.fingerprint = "fnv1a64-v1:0123456789abcdef0";
    REQUIRE_FALSE(
        pnga::statistics::serialize_statistics_json(document, ready_snapshot())
            .success);
  }
  SECTION("complete without ready status") {
    auto snapshot = ready_snapshot();
    snapshot.tokens.state.status = SectionStatus::kPartial;
    const auto result = pnga::statistics::serialize_statistics_json(
        valid_identity(), snapshot);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "contradictory state for section tokens");
  }
  SECTION("ready and complete without whole_document scope") {
    auto snapshot = ready_snapshot();
    snapshot.blocks.state.scope = SectionScope::kVerifiedPrefix;
    const auto result = pnga::statistics::serialize_statistics_csv(
        valid_identity(), snapshot);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "contradictory state for section blocks");
  }
}

TEST_CASE("Serializers accept accumulated snapshots",
          "[statistics][wp602d]") {
  const std::array chunks = {pnga::statistics::ChunkSample{"IDAT", 12}};
  const auto collected = pnga::statistics::collect(
      pnga::statistics::StatisticsInput{chunks, {}, {}, {}}, {}, {});
  const DocumentIdentity document{4096, "fnv1a64-v1:0000000000000001"};
  const auto json =
      pnga::statistics::serialize_statistics_json(document, collected);
  const auto csv =
      pnga::statistics::serialize_statistics_csv(document, collected);
  REQUIRE(json.success);
  REQUIRE(csv.success);
  assert_report_bytes(json.bytes);
  assert_report_bytes(csv.bytes);
}

TEST_CASE("Unknown compression totals serialize as absent values",
          "[statistics][wp602-quality]") {
  const auto document = valid_identity();
  const auto make_unknown_totals = [](SectionStatus status) {
    auto snapshot = ready_snapshot();
    snapshot.overview.state.status = status;
    snapshot.overview.state.complete = false;
    snapshot.overview.state.scope = SectionScope::kNone;
    snapshot.overview.state.error = "statistics sample budget exceeded";
    snapshot.overview.data.compressed_bytes = 0;
    snapshot.overview.data.inflated_bytes = 0;
    snapshot.overview.data.has_compression_totals = false;
    return snapshot;
  };

  for (const SectionStatus status :
       {SectionStatus::kPartial, SectionStatus::kInvalidInput,
        SectionStatus::kBudgetExceeded}) {
    const auto snapshot = make_unknown_totals(status);
    CAPTURE(std::to_string(static_cast<int>(status)));
    const auto json =
        pnga::statistics::serialize_statistics_json(document, snapshot);
    const auto csv =
        pnga::statistics::serialize_statistics_csv(document, snapshot);
    REQUIRE(json.success);
    REQUIRE(csv.success);
    assert_report_bytes(json.bytes);
    assert_report_bytes(csv.bytes);
    // The totals pair is jointly unavailable: JSON null, CSV empty — never
    // a zero value.
    REQUIRE(json.bytes.find("\"compressed_bytes\": null") != std::string::npos);
    REQUIRE(json.bytes.find("\"inflated_bytes\": null") != std::string::npos);
    REQUIRE(csv.bytes.find("1,overview,compressed_bytes,,,bytes\n") !=
            std::string::npos);
    REQUIRE(csv.bytes.find("1,overview,inflated_bytes,,,bytes\n") !=
            std::string::npos);
  }
}

TEST_CASE("Verified zero totals keep their numeric zero output",
          "[statistics][wp602-quality]") {
  const auto document = valid_identity();
  auto snapshot = ready_snapshot();
  REQUIRE(snapshot.overview.data.has_compression_totals);
  snapshot.overview.data.compressed_bytes = 0;
  snapshot.overview.data.inflated_bytes = 0;
  const auto json =
      pnga::statistics::serialize_statistics_json(document, snapshot);
  const auto csv =
      pnga::statistics::serialize_statistics_csv(document, snapshot);
  REQUIRE(json.success);
  REQUIRE(csv.success);
  REQUIRE(json.bytes.find("\"compressed_bytes\": 0") != std::string::npos);
  REQUIRE(json.bytes.find("\"inflated_bytes\": 0") != std::string::npos);
  REQUIRE(csv.bytes.find("1,overview,compressed_bytes,,0,bytes\n") !=
          std::string::npos);
  REQUIRE(csv.bytes.find("1,overview,inflated_bytes,,0,bytes\n") !=
          std::string::npos);
}
