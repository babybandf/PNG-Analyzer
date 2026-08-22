#ifndef PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_BUNDLE_H
#define PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_BUNDLE_H

// M5 Trace Gate: one immutable projection boundary shared by the three
// Deflate inspector pages. The bundle also owns the explicitly requested,
// budgeted Trace-to-Original-Literal walk; GUI code only consumes its result.

#include "pnga/analysis-engine/block_inspector.h"
#include "pnga/analysis-engine/decode_trace_inspector.h"
#include "pnga/analysis-engine/huffman_inspector.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

struct TraceInspectorBundle {
  std::uint64_t generation = 0;
  BlockInspectorView block;
  HuffmanInspectorView huffman;
  DecodeTraceInspectorView decode;

  bool operator==(const TraceInspectorBundle&) const = default;
};

TraceInspectorBundle build_trace_inspector_bundle(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index = std::nullopt,
    std::optional<std::uint64_t> selected_output_offset = std::nullopt,
    std::optional<std::uint64_t> scanline = std::nullopt);

enum class TraceLiteralWalkStatus {
  kReady = 0,
  kNotFound = 1,
  kPartial = 2,
  kBudgetExceeded = 3,
  kError = 4,
};

const char* trace_literal_walk_status_text(TraceLiteralWalkStatus status) noexcept;

struct TraceLiteralWalkResult {
  TraceLiteralWalkStatus status = TraceLiteralWalkStatus::kNotFound;
  std::string error;
  std::vector<std::uint64_t> token_path;
  std::uint64_t depth_limit = 0;
  std::uint64_t node_budget = 0;

  bool operator==(const TraceLiteralWalkResult&) const = default;
};

// Follows match_source_ranges only when the user explicitly asks. The walk is
// cycle-safe and bounded by both depth and visited-node budgets.
TraceLiteralWalkResult trace_to_original_literal(
    const TraceQueryResult& trace, std::uint64_t token_index,
    std::uint64_t max_depth, std::uint64_t max_nodes);

struct TraceNavigationRange {
  enum class Space { kLogicalDeflate, kInflatedOutput };
  Space space = Space::kLogicalDeflate;
  std::uint64_t begin = 0;
  std::uint64_t end = 0;

  bool operator==(const TraceNavigationRange&) const = default;
};

std::optional<TraceNavigationRange> trace_token_navigation(
    const TraceQueryResult& trace, std::uint64_t token_index,
    TraceNavigationRange::Space space);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_BUNDLE_H
