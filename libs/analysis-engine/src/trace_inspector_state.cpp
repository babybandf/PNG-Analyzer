// WP-5U6A Trace Inspector lifecycle state machine.

#include "pnga/analysis-engine/trace_inspector_state.h"

namespace pnga::analysis_engine {

const char* trace_inspector_lifecycle_text(
    TraceInspectorLifecycle status) noexcept {
  switch (status) {
    case TraceInspectorLifecycle::kEmpty:
      return "empty";
    case TraceInspectorLifecycle::kLoading:
      return "loading";
    case TraceInspectorLifecycle::kReplaying:
      return "replaying";
    case TraceInspectorLifecycle::kReady:
      return "ready";
    case TraceInspectorLifecycle::kPartial:
      return "partial";
    case TraceInspectorLifecycle::kError:
      return "error";
    case TraceInspectorLifecycle::kCancelled:
      return "cancelled";
    case TraceInspectorLifecycle::kStaleGeneration:
      return "stale generation";
  }
  return "unknown";
}

void TraceInspectorStateMachine::replaceDocument(
    std::uint64_t generation) noexcept {
  state_ = TraceInspectorState{};
  state_.generation = generation;
}

void TraceInspectorStateMachine::beginLoading(
    std::uint64_t generation) noexcept {
  if (generation != state_.generation) {
    replaceDocument(generation);
  }
  state_.status = TraceInspectorLifecycle::kLoading;
  state_.error.clear();
  state_.bundle.reset();
}

void TraceInspectorStateMachine::markReplaying(
    std::uint64_t generation) noexcept {
  if (!accepts(generation)) {
    state_.status = TraceInspectorLifecycle::kStaleGeneration;
    return;
  }
  state_.status = TraceInspectorLifecycle::kReplaying;
  state_.error.clear();
}

bool TraceInspectorStateMachine::publish(
    const TraceQueryResult& result,
    std::optional<std::uint64_t> selected_token_index,
    std::optional<std::uint64_t> selected_output_offset,
    std::optional<std::uint64_t> scanline) {
  if (!accepts(result.generation)) {
    // Keep the current document's state and make the stale event observable;
    // importantly, do not replace a newer bundle with old data.
    state_.status = TraceInspectorLifecycle::kStaleGeneration;
    state_.error = "trace result generation is stale";
    return false;
  }
  state_.bundle = build_trace_inspector_bundle(
      result, selected_token_index, selected_output_offset, scanline);
  state_.error = result.error;
  switch (result.status) {
    case TraceQueryStatus::kReady:
      state_.status = TraceInspectorLifecycle::kReady;
      break;
    case TraceQueryStatus::kPartial:
      state_.status = TraceInspectorLifecycle::kPartial;
      break;
    case TraceQueryStatus::kCancelled:
      state_.status = TraceInspectorLifecycle::kCancelled;
      break;
    case TraceQueryStatus::kError:
      state_.status = TraceInspectorLifecycle::kError;
      break;
    case TraceQueryStatus::kNotIndexed:
    case TraceQueryStatus::kReplaying:
      state_.status = TraceInspectorLifecycle::kReplaying;
      break;
  }
  return true;
}

bool TraceInspectorStateMachine::cancel(std::uint64_t generation) noexcept {
  if (!accepts(generation)) {
    state_.status = TraceInspectorLifecycle::kStaleGeneration;
    return false;
  }
  state_.status = TraceInspectorLifecycle::kCancelled;
  state_.error.clear();
  return true;
}

bool TraceInspectorStateMachine::accepts(
    std::uint64_t generation) const noexcept {
  return generation == state_.generation;
}

}  // namespace pnga::analysis_engine
