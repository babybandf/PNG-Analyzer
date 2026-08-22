#ifndef PNGA_ANALYSIS_ENGINE_STAGE_VIEWPORT_H
#define PNGA_ANALYSIS_ENGINE_STAGE_VIEWPORT_H

// WP-5U3B: bounded viewport access to immutable stage data. The provider
// exposes native sample windows for the Pixels view without creating another
// full-size image or retaining GUI objects.

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace pnga::analysis_engine {

enum class ViewportStatus {
  kReady,
  kNoStage,
  kNotApplicable,
  kOutOfRange,
  kBudgetExceeded,
  kError,
};

const char* viewport_status_text(ViewportStatus status) noexcept;

struct ViewportRequest {
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kNative;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t width = 1;
  std::uint64_t height = 1;
  std::uint64_t max_samples = 65536;

  bool operator==(const ViewportRequest&) const = default;
};

struct StageViewport {
  ViewportStatus status = ViewportStatus::kError;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::uint8_t channels = 0;
  std::vector<std::uint16_t> samples;
  std::uint64_t cache_generation = 0;
};

// A bounded, deterministic provider over one immutable StageSet. The cache is
// deliberately one request wide: it avoids retaining a second image-sized
// surface while keeping repeated hover/selection refreshes cheap.
class StageViewportProvider final {
 public:
  explicit StageViewportProvider(
      std::shared_ptr<const StageSet> stages = nullptr);

  void setStageSet(std::shared_ptr<const StageSet> stages);
  void clear() noexcept;
  std::shared_ptr<const StageSet> stageSet() const noexcept { return stages_; }

  std::shared_ptr<const StageViewport> query(const ViewportRequest& request);
  std::uint64_t cacheGeneration() const noexcept { return cache_generation_; }

 private:
  std::shared_ptr<const StageSet> stages_;
  std::optional<ViewportRequest> cached_request_;
  std::shared_ptr<const StageViewport> cached_result_;
  std::uint64_t cache_generation_ = 0;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STAGE_VIEWPORT_H
