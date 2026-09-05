// WP-602F: snapshot-to-row projection. Deterministic ids and order mirror
// the fixed metric order of the shared serializer; values are raw integers
// (no percentages, no thousands separators — formatting belongs to the GUI
// display role). A value is present whenever the owning section is not
// unavailable, so verified partial evidence stays visible while missing
// data never becomes a zero.

#include "pnga/analysis-engine/statistics_view.h"

#include <string>

namespace pnga::analysis_engine {
namespace {

using pnga::statistics::SectionStatus;

// The serializer's availability rule: only an unavailable section suppresses
// values; every collected status keeps its verified totals.
bool section_available(const pnga::statistics::SectionState& state) noexcept {
  return state.status != SectionStatus::kUnavailable;
}

StatisticsRow make_row(std::string id, std::string group, std::string label,
                       std::optional<std::uint64_t> value, std::string unit,
                       const pnga::statistics::SectionState& state) {
  StatisticsRow row;
  row.id = std::move(id);
  row.group = std::move(group);
  row.label = std::move(label);
  row.value = value;
  row.unit = std::move(unit);
  row.state = state;
  return row;
}

StatisticsNavigationRequest make_navigation(
    std::uint64_t generation, StatisticsBucketDomain domain, std::string key) {
  StatisticsNavigationRequest request;
  request.generation = generation;
  request.domain = domain;
  request.key = std::move(key);
  request.direction = OccurrenceDirection::kFirst;
  request.max_tokens = kOccurrenceMaxTokens;
  request.max_input_bytes = kOccurrenceMaxInputBytes;
  return request;
}

// Bucket rows keep their navigation whenever the owning section carries
// evidence; an unavailable section offers nothing to navigate.
StatisticsRow make_bucket_row(std::string id, std::string group,
                              std::string label,
                              std::optional<std::uint64_t> value,
                              std::string unit,
                              const pnga::statistics::SectionState& state,
                              std::uint64_t generation,
                              StatisticsBucketDomain domain, std::string key) {
  StatisticsRow row =
      make_row(std::move(id), std::move(group), std::move(label), value,
               std::move(unit), state);
  if (section_available(state)) {
    row.navigation = make_navigation(generation, domain, std::move(key));
  }
  return row;
}

}  // namespace

StatisticsView build_statistics_view(
    std::uint64_t generation,
    const pnga::statistics::StatisticsSnapshot& snapshot) {
  StatisticsView view;
  view.generation = generation;

  const bool overview_available = section_available(snapshot.overview.state);
  const std::optional<std::uint64_t> compressed =
      overview_available && snapshot.overview.data.has_compression_totals
          ? std::optional<std::uint64_t>{
                snapshot.overview.data.compressed_bytes}
          : std::nullopt;
  const std::optional<std::uint64_t> inflated =
      overview_available && snapshot.overview.data.has_compression_totals
          ? std::optional<std::uint64_t>{snapshot.overview.data.inflated_bytes}
          : std::nullopt;
  view.overview.push_back(make_row("overview.compressed_bytes", "overview",
                                   "Compressed bytes", compressed, "bytes",
                                   snapshot.overview.state));
  view.overview.push_back(make_row("overview.inflated_bytes", "overview",
                                   "Inflated bytes", inflated, "bytes",
                                   snapshot.overview.state));

  const bool chunks_available = section_available(snapshot.chunks.state);
  view.chunks.push_back(make_row(
      "chunks.count", "chunks", "Chunks",
      chunks_available ? std::optional<std::uint64_t>{
                             snapshot.chunks.data.count}
                       : std::nullopt,
      "chunks", snapshot.chunks.state));
  view.chunks.push_back(make_row(
      "chunks.data_bytes", "chunks", "Chunk data bytes",
      chunks_available ? std::optional<std::uint64_t>{
                             snapshot.chunks.data.data_bytes}
                       : std::nullopt,
      "bytes", snapshot.chunks.state));
  if (chunks_available) {
    for (const pnga::statistics::ChunkBucket& bucket :
         snapshot.chunks.data.buckets) {
      view.chunks.push_back(make_bucket_row(
          "chunks." + bucket.type, "chunks", bucket.type, bucket.count,
          "chunks", snapshot.chunks.state, generation,
          StatisticsBucketDomain::kChunkType, bucket.type));
    }
  }

  const bool filters_available = section_available(snapshot.filters.state);
  view.filters.push_back(make_row(
      "filters.rows", "filters", "Filter rows",
      filters_available
          ? std::optional<std::uint64_t>{snapshot.filters.data.rows}
          : std::nullopt,
      "rows", snapshot.filters.state));
  view.filters.push_back(make_row(
      "filters.data_bytes", "filters", "Filtered bytes",
      filters_available
          ? std::optional<std::uint64_t>{snapshot.filters.data.data_bytes}
          : std::nullopt,
      "bytes", snapshot.filters.state));
  view.filters.push_back(make_row(
      "filters.invalid_rows", "filters", "Invalid rows",
      filters_available
          ? std::optional<std::uint64_t>{snapshot.filters.data.invalid_rows}
          : std::nullopt,
      "rows", snapshot.filters.state));
  // The filter buckets are fixed at five entries (types 0..4), so the rows
  // exist even while the section is unavailable — with no value and no
  // navigation until evidence arrives.
  for (std::size_t i = 0; i < 5; ++i) {
    const std::string key = std::to_string(i);
    std::optional<std::uint64_t> value;
    if (filters_available && i < snapshot.filters.data.buckets.size()) {
      value = snapshot.filters.data.buckets[i].rows;
    }
    view.filters.push_back(make_bucket_row(
        "filters." + key, "filters", "Filter " + key, value, "rows",
        snapshot.filters.state, generation, StatisticsBucketDomain::kFilterType,
        key));
  }

  const bool blocks_available = section_available(snapshot.blocks.state);
  view.deflate.push_back(make_row(
      "blocks.count", "blocks", "Blocks",
      blocks_available ? std::optional<std::uint64_t>{
                             snapshot.blocks.data.count}
                       : std::nullopt,
      "blocks", snapshot.blocks.state));
  view.deflate.push_back(make_row(
      "blocks.compressed_bits", "blocks", "Compressed bits",
      blocks_available ? std::optional<std::uint64_t>{
                             snapshot.blocks.data.compressed_bits}
                       : std::nullopt,
      "bits", snapshot.blocks.state));
  view.deflate.push_back(make_row(
      "blocks.output_bytes", "blocks", "Output bytes",
      blocks_available ? std::optional<std::uint64_t>{
                             snapshot.blocks.data.output_bytes}
                       : std::nullopt,
      "bytes", snapshot.blocks.state));
  static constexpr const char* kBlockKeys[] = {"stored", "fixed", "dynamic"};
  static constexpr const char* kBlockLabels[] = {"Stored blocks",
                                                 "Fixed blocks",
                                                 "Dynamic blocks"};
  for (std::size_t i = 0; i < 3; ++i) {
    std::optional<std::uint64_t> value;
    if (blocks_available && i < snapshot.blocks.data.buckets.size()) {
      value = snapshot.blocks.data.buckets[i].blocks;
    }
    view.deflate.push_back(make_bucket_row(
        std::string("blocks.") + kBlockKeys[i], "blocks", kBlockLabels[i],
        value, "blocks", snapshot.blocks.state, generation,
        StatisticsBucketDomain::kBlockType, kBlockKeys[i]));
  }

  const bool tokens_available = section_available(snapshot.tokens.state);
  view.deflate.push_back(make_row(
      "tokens.count", "tokens", "Tokens",
      tokens_available ? std::optional<std::uint64_t>{
                             snapshot.tokens.data.count}
                       : std::nullopt,
      "tokens", snapshot.tokens.state));
  view.deflate.push_back(make_row(
      "tokens.input_bits", "tokens", "Input bits",
      tokens_available ? std::optional<std::uint64_t>{
                             snapshot.tokens.data.input_bits}
                       : std::nullopt,
      "bits", snapshot.tokens.state));
  view.deflate.push_back(make_row(
      "tokens.output_bytes", "tokens", "Output bytes",
      tokens_available ? std::optional<std::uint64_t>{
                             snapshot.tokens.data.output_bytes}
                       : std::nullopt,
      "bytes", snapshot.tokens.state));
  static constexpr const char* kTokenKeys[] = {"literal", "match", "eob"};
  static constexpr const char* kTokenLabels[] = {"Literal tokens",
                                                 "Match tokens",
                                                 "End-of-block tokens"};
  for (std::size_t i = 0; i < 3; ++i) {
    std::optional<std::uint64_t> value;
    if (tokens_available && i < snapshot.tokens.data.buckets.size()) {
      value = snapshot.tokens.data.buckets[i].count;
    }
    view.deflate.push_back(make_bucket_row(
        std::string("tokens.") + kTokenKeys[i], "tokens", kTokenLabels[i],
        value, "tokens", snapshot.tokens.state, generation,
        StatisticsBucketDomain::kTokenKind, kTokenKeys[i]));
  }

  const bool lengths_available = section_available(snapshot.lengths.state);
  if (lengths_available) {
    for (const pnga::statistics::ValueBucket& bucket :
         snapshot.lengths.data.buckets) {
      const std::string key = std::to_string(bucket.value);
      view.deflate.push_back(make_bucket_row(
          "lengths." + key, "lengths", "Length " + key, bucket.count,
          "matches", snapshot.lengths.state, generation,
          StatisticsBucketDomain::kLength, key));
    }
  }
  const bool distances_available = section_available(snapshot.distances.state);
  if (distances_available) {
    for (const pnga::statistics::ValueBucket& bucket :
         snapshot.distances.data.buckets) {
      const std::string key = std::to_string(bucket.value);
      view.deflate.push_back(make_bucket_row(
          "distances." + key, "distances", "Distance " + key, bucket.count,
          "matches", snapshot.distances.state, generation,
          StatisticsBucketDomain::kDistance, key));
    }
  }

  return view;
}

}  // namespace pnga::analysis_engine
