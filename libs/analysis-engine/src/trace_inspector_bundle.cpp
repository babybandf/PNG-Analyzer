// M5 Trace Gate bundle and bounded literal-origin walk.

#include "pnga/analysis-engine/trace_inspector_bundle.h"

#include <unordered_set>

namespace pnga::analysis_engine {

namespace {

const TraceTokenSummary* find_token(const TraceQueryResult& trace,
                                    std::uint64_t index) noexcept {
  for (const auto& token : trace.tokens) {
    if (token.index == index) {
      return &token;
    }
  }
  return nullptr;
}

}  // namespace

TraceInspectorBundle build_trace_inspector_bundle(
    const TraceQueryResult& trace,
    std::optional<std::uint64_t> selected_token_index,
    std::optional<std::uint64_t> selected_output_offset,
    std::optional<std::uint64_t> scanline) {
  TraceInspectorBundle bundle;
  bundle.generation = trace.generation;
  bundle.block = build_block_inspector(trace, selected_output_offset, scanline);
  bundle.huffman = build_huffman_inspector(trace, selected_token_index);
  bundle.decode = build_decode_trace_inspector(
      trace, selected_token_index, selected_output_offset);
  return bundle;
}

const char* trace_literal_walk_status_text(
    TraceLiteralWalkStatus status) noexcept {
  switch (status) {
    case TraceLiteralWalkStatus::kReady:
      return "ready";
    case TraceLiteralWalkStatus::kNotFound:
      return "not_found";
    case TraceLiteralWalkStatus::kPartial:
      return "partial";
    case TraceLiteralWalkStatus::kBudgetExceeded:
      return "budget_exceeded";
    case TraceLiteralWalkStatus::kError:
      return "error";
  }
  return "unknown";
}

TraceLiteralWalkResult trace_to_original_literal(
    const TraceQueryResult& trace, std::uint64_t token_index,
    std::uint64_t max_depth, std::uint64_t max_nodes) {
  TraceLiteralWalkResult result;
  result.depth_limit = max_depth;
  result.node_budget = max_nodes;
  if (max_depth == 0 || max_nodes == 0) {
    result.status = TraceLiteralWalkStatus::kBudgetExceeded;
    result.error = "trace-to-literal budget is zero";
    return result;
  }
  const TraceTokenSummary* current = find_token(trace, token_index);
  if (current == nullptr) {
    result.status = TraceLiteralWalkStatus::kNotFound;
    result.error = "token is not present in the bounded trace";
    return result;
  }
  std::unordered_set<std::uint64_t> visited;
  std::uint64_t depth = 0;
  while (current != nullptr) {
    if (depth >= max_depth || result.token_path.size() >= max_nodes) {
      result.status = TraceLiteralWalkStatus::kBudgetExceeded;
      result.error = "trace-to-literal budget exceeded";
      return result;
    }
    if (!visited.insert(current->index).second) {
      result.status = TraceLiteralWalkStatus::kError;
      result.error = "cycle in match source provenance";
      return result;
    }
    result.token_path.push_back(current->index);
    if (current->kind == pnga::deflate_trace::TokenKind::kLiteral) {
      result.status = TraceLiteralWalkStatus::kReady;
      return result;
    }
    if (current->kind != pnga::deflate_trace::TokenKind::kLengthDistance ||
        current->match_source_ranges.empty()) {
      result.status = TraceLiteralWalkStatus::kNotFound;
      result.error = "token has no original literal source";
      return result;
    }
    // A match can have multiple root ranges. Follow the first deterministic
    // source; callers can inspect all ranges in the Decode Trace row.
    const std::uint64_t source_token =
        current->match_source_ranges.front().token_index;
    current = find_token(trace, source_token);
    ++depth;
  }
  result.status = TraceLiteralWalkStatus::kNotFound;
  result.error = "match source token is outside the bounded trace";
  return result;
}

std::optional<TraceNavigationRange> trace_token_navigation(
    const TraceQueryResult& trace, std::uint64_t token_index,
    TraceNavigationRange::Space space) {
  const auto* token = find_token(trace, token_index);
  if (token == nullptr) {
    return std::nullopt;
  }
  if (space == TraceNavigationRange::Space::kLogicalDeflate) {
    return TraceNavigationRange{space, token->input_bit_begin,
                                token->input_bit_end};
  }
  return TraceNavigationRange{space, token->output_begin, token->output_end};
}

}  // namespace pnga::analysis_engine
