#ifndef PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_STATE_H
#define PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_STATE_H

// WP-5U6A: generation-safe lifecycle state for the bounded Trace Inspector.
// This state machine owns no worker and performs no decoding; it is the
// deterministic boundary used by Qt and orchestration callbacks.

#include "pnga/analysis-engine/trace_inspector_bundle.h"

#include <cstdint>
#include <optional>
#include <string>

namespace pnga::analysis_engine {

enum class TraceInspectorLifecycle {
  kEmpty = 0,
  kLoading = 1,
  kReplaying = 2,
  kReady = 3,
  kPartial = 4,
  kError = 5,
  kCancelled = 6,
  kStaleGeneration = 7,
};

const char* trace_inspector_lifecycle_text(
    TraceInspectorLifecycle status) noexcept;

struct TraceInspectorState {
  TraceInspectorLifecycle status = TraceInspectorLifecycle::kEmpty;
  std::uint64_t generation = 0;
  std::string error;
  std::optional<TraceInspectorBundle> bundle;

  bool operator==(const TraceInspectorState&) const = default;
};

class TraceInspectorStateMachine final {
 public:
  TraceInspectorStateMachine() = default;

  const TraceInspectorState& state() const noexcept { return state_; }

  // Starts a new document and invalidates every older callback.
  void replaceDocument(std::uint64_t generation) noexcept;
  void beginLoading(std::uint64_t generation) noexcept;
  void markReplaying(std::uint64_t generation) noexcept;
  bool publish(const TraceQueryResult& result,
               std::optional<std::uint64_t> selected_token_index = std::nullopt,
               std::optional<std::uint64_t> selected_output_offset =
                   std::nullopt,
               std::optional<std::uint64_t> scanline = std::nullopt);
  bool cancel(std::uint64_t generation) noexcept;

 private:
  bool accepts(std::uint64_t generation) const noexcept;
  TraceInspectorState state_;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_TRACE_INSPECTOR_STATE_H
