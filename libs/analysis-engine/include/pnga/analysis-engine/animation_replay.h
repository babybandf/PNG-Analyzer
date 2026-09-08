#ifndef PNGA_ANALYSIS_ENGINE_ANIMATION_REPLAY_H
#define PNGA_ANALYSIS_ENGINE_ANIMATION_REPLAY_H

#include "pnga/analysis-engine/frame_analysis.h"

#include <cstdint>
#include <list>
#include <map>
#include <memory>

namespace pnga::analysis_engine {

struct ReplayRequest {
  FrameRequest frame;
  pnga::trace_model::Stage requested_stage =
      pnga::trace_model::Stage::kFrameOutput;
};

struct ReplayResult {
  using Stop = FrameResult::Stop;

  std::uint64_t generation = 0;
  std::uint64_t request_serial = 0;
  pnga::trace_model::ImageIdentity identity =
      pnga::trace_model::AnimationFrame{0};
  Stop stop = Stop::kError;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;
  std::shared_ptr<const pnga::png_reconstruction::RgbaImage> image;
  std::string reason;
};

class AnimationReplay {
 public:
  explicit AnimationReplay(std::uint64_t budget_bytes);

  ReplayResult materialize(const ReplayRequest& request,
                           const CancellationToken* cancellation);
  void clear();
  std::uint64_t retained_bytes() const noexcept { return retained_bytes_; }

 private:
  struct CacheKey {
    std::uint64_t generation = 0;
    std::uint32_t ordinal = 0;
    pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;

    bool operator<(const CacheKey& other) const noexcept;
  };
  struct CacheEntry {
    CacheKey key;
    std::shared_ptr<const pnga::png_reconstruction::RgbaImage> image;
    std::uint64_t bytes = 0;
  };

  std::shared_ptr<const pnga::png_reconstruction::RgbaImage> cached(
      const CacheKey& key);
  bool cache(const CacheKey& key,
             std::shared_ptr<const pnga::png_reconstruction::RgbaImage> image);

  std::uint64_t budget_ = 0;
  std::uint64_t retained_bytes_ = 0;
  std::list<CacheEntry> lru_;
  std::map<CacheKey, std::list<CacheEntry>::iterator> entries_;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_ANIMATION_REPLAY_H
