#ifndef PNGA_ANALYSIS_ENGINE_STATISTICS_VIEW_H
#define PNGA_ANALYSIS_ENGINE_STATISTICS_VIEW_H

// WP-602F: immutable statistics page rows and typed navigation requests. The
// projection turns a StatisticsSnapshot into fixed, deterministic rows for
// the four Statistics pages (overview, chunks, filters, deflate). Rows carry
// stable ids, raw unformatted integers, units, the owning section state and
// — for bucket rows — an optional navigation request with the frozen
// occurrence budgets. Qt-free (ADR-0003); the GUI only formats these rows.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <pnga/statistics/statistics.h>

namespace pnga::analysis_engine {

// The four frozen Statistics pages (WP-602G ruling R11). The DEFLATE page
// presents blocks, tokens, lengths and distances as grouped rows instead of
// a fifth internal page.
enum class StatisticsPage { kOverview, kChunks, kFilters, kDeflate };

// The bucket identity of a navigation request. Chunk/filter/block resolve
// directly from the existing indexes; token/length/distance use the bounded
// occurrence query.
enum class StatisticsBucketDomain {
  kChunkType,
  kFilterType,
  kBlockType,
  kTokenKind,
  kLength,
  kDistance,
};

enum class OccurrenceDirection { kFirst, kPrevious, kNext };

// Frozen occurrence budgets (WP-602F ruling R6): a token occurrence scan is
// bounded to 4,096 tokens and 8 MiB of input.
inline constexpr std::uint64_t kOccurrenceMaxTokens = 4096;
inline constexpr std::uint64_t kOccurrenceMaxInputBytes = 8ull << 20;

struct StatisticsNavigationRequest {
  std::uint64_t generation = 0;
  StatisticsBucketDomain domain = StatisticsBucketDomain::kChunkType;
  std::string key;
  OccurrenceDirection direction = OccurrenceDirection::kFirst;
  // Output cursor: next/previous resolve strictly after/before this inflated
  // byte offset (direct domains use their own occurrence position).
  std::optional<std::uint64_t> after_output_offset;
  std::uint64_t max_tokens = kOccurrenceMaxTokens;
  std::uint64_t max_input_bytes = kOccurrenceMaxInputBytes;
};

// One immutable row. `value` is the raw integer without any locale
// formatting; std::nullopt marks an unavailable value. `state` is the owning
// section state so unavailable rows never turn into ready zero values.
struct StatisticsRow {
  std::string id;
  std::string group;
  std::string label;
  std::optional<std::uint64_t> value;
  std::string unit;
  pnga::statistics::SectionState state;
  std::optional<StatisticsNavigationRequest> navigation;
};

struct StatisticsView {
  std::uint64_t generation = 0;
  std::vector<StatisticsRow> overview;
  std::vector<StatisticsRow> chunks;
  std::vector<StatisticsRow> filters;
  std::vector<StatisticsRow> deflate;
};

// Projects the snapshot into the fixed row set: overview totals, chunk
// totals and sorted bucket rows, the three filter totals plus the fixed
// filter buckets 0-4, and the DEFLATE rows in blocks/tokens/lengths/
// distances group order. Row ids, groups and order are deterministic and
// never depend on locale, clock or iteration order. Bucket navigation
// requests carry `generation` and the frozen budgets.
StatisticsView build_statistics_view(
    std::uint64_t generation,
    const pnga::statistics::StatisticsSnapshot& snapshot);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STATISTICS_VIEW_H
