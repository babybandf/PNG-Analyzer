#include "pnga/analysis-engine/animation_replay.h"

#include <pnga/png-reconstruction/canvas_composition.h>

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace pnga::analysis_engine {

namespace {

using pnga::png_reconstruction::Blend;
using pnga::png_reconstruction::Dispose;
using pnga::png_reconstruction::FrameRect;
using pnga::png_reconstruction::RgbaImage;

bool image_bytes(const RgbaImage& image, std::uint64_t* bytes) noexcept {
  const std::uint64_t area =
      static_cast<std::uint64_t>(image.width) * image.height;
  if (area > std::numeric_limits<std::uint64_t>::max() / 4) {
    return false;
  }
  *bytes = area * 4;
  return *bytes == image.pixels.size();
}

ReplayResult base_result(const ReplayRequest& request) {
  ReplayResult result;
  result.generation = request.frame.generation;
  result.request_serial = request.frame.request_serial;
  result.identity = pnga::trace_model::ImageIdentity{
      pnga::trace_model::AnimationFrame{request.frame.ordinal}};
  result.stage = request.requested_stage;
  return result;
}

ReplayResult stopped(ReplayResult result, ReplayResult::Stop stop,
                     std::string reason) {
  result.stop = stop;
  result.reason = std::move(reason);
  result.image.reset();
  return result;
}

bool is_canvas_stage(pnga::trace_model::Stage stage) noexcept {
  return stage == pnga::trace_model::Stage::kPreBlend ||
         stage == pnga::trace_model::Stage::kPostBlend ||
         stage == pnga::trace_model::Stage::kPostDispose;
}

}  // namespace

bool AnimationReplay::CacheKey::operator<(const CacheKey& other) const noexcept {
  return std::tie(generation, ordinal, stage) <
         std::tie(other.generation, other.ordinal, other.stage);
}

AnimationReplay::AnimationReplay(std::uint64_t budget_bytes)
    : budget_(budget_bytes) {}

std::shared_ptr<const RgbaImage> AnimationReplay::cached(
    const CacheKey& key) {
  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return nullptr;
  }
  lru_.splice(lru_.end(), lru_, it->second);
  return it->second->image;
}

bool AnimationReplay::cache(
    const CacheKey& key,
    std::shared_ptr<const pnga::png_reconstruction::RgbaImage> image) {
  if (!image) {
    return false;
  }
  std::uint64_t bytes = 0;
  if (!image_bytes(*image, &bytes) || bytes > budget_) {
    return false;
  }
  const auto existing = entries_.find(key);
  if (existing != entries_.end()) {
    retained_bytes_ -= existing->second->bytes;
    lru_.erase(existing->second);
    entries_.erase(existing);
  }
  while (retained_bytes_ > budget_ - bytes && !lru_.empty()) {
    const auto old = lru_.begin();
    retained_bytes_ -= old->bytes;
    entries_.erase(old->key);
    lru_.pop_front();
  }
  lru_.push_back(CacheEntry{key, std::move(image), bytes});
  auto inserted = std::prev(lru_.end());
  entries_[key] = inserted;
  retained_bytes_ += bytes;
  return true;
}

void AnimationReplay::clear() {
  entries_.clear();
  lru_.clear();
  retained_bytes_ = 0;
}

ReplayResult AnimationReplay::materialize(
    const ReplayRequest& request, const CancellationToken* cancellation) {
  ReplayResult result = base_result(request);
  const auto cancelled = [cancellation]() {
    return cancellation != nullptr && cancellation->cancelled();
  };
  if (cancelled()) {
    return stopped(std::move(result), ReplayResult::Stop::kCancelled,
                   "animation replay cancelled");
  }
  if (!request.frame.index || !request.frame.source ||
      request.frame.ordinal >= request.frame.index->frames.size()) {
    return stopped(std::move(result), ReplayResult::Stop::kError,
                   "frame ordinal is outside the verified prefix");
  }
  if (request.requested_stage != pnga::trace_model::Stage::kFrameOutput &&
      !is_canvas_stage(request.requested_stage)) {
    return stopped(std::move(result), ReplayResult::Stop::kError,
                   "stage is not an animation replay stage");
  }

  const CacheKey key{request.frame.generation, request.frame.ordinal,
                     request.requested_stage};
  if (const auto image = cached(key)) {
    result.stop = ReplayResult::Stop::kReady;
    result.image = image;
    return result;
  }

  const auto frame_image = [&](std::uint32_t ordinal,
                               std::string* error)
      -> std::shared_ptr<const RgbaImage> {
    FrameRequest frame_request = request.frame;
    frame_request.ordinal = ordinal;
    const FrameResult frame = analyze_frame(frame_request, cancellation);
    if (frame.stop != FrameResult::Stop::kReady || !frame.frame) {
      *error = frame.error.empty() ? "frame analysis failed" : frame.error;
      return nullptr;
    }
    return std::make_shared<const RgbaImage>(frame.frame->delivered);
  };

  if (request.requested_stage == pnga::trace_model::Stage::kFrameOutput) {
    std::string error;
    const auto image = frame_image(request.frame.ordinal, &error);
    if (!image) {
      const auto stop = cancelled() ? ReplayResult::Stop::kCancelled
                                    : ReplayResult::Stop::kError;
      return stopped(std::move(result), stop, std::move(error));
    }
    if (!cache(key, image)) {
      return stopped(std::move(result), ReplayResult::Stop::kPartial,
                     "animation artifact exceeds the cache budget");
    }
    result.stop = ReplayResult::Stop::kReady;
    result.image = image;
    return result;
  }

  const auto canvas_bytes = static_cast<std::uint64_t>(
      request.frame.canvas_header.width) * request.frame.canvas_header.height;
  if (canvas_bytes > std::numeric_limits<std::uint64_t>::max() / 4) {
    return stopped(std::move(result), ReplayResult::Stop::kPartial,
                   "canvas size overflows");
  }
  RgbaImage canvas;
  canvas.width = request.frame.canvas_header.width;
  canvas.height = request.frame.canvas_header.height;
  canvas.pixels.assign(static_cast<std::size_t>(canvas_bytes * 4), 0);

  const bool include_current =
      request.requested_stage != pnga::trace_model::Stage::kPreBlend;
  const std::uint32_t last = request.frame.ordinal;
  for (std::uint32_t ordinal = 0; ordinal <= last; ++ordinal) {
    if (cancelled()) {
      return stopped(std::move(result), ReplayResult::Stop::kCancelled,
                     "animation replay cancelled");
    }
    if (!include_current && ordinal == last) {
      break;
    }
    std::string error;
    const auto frame = frame_image(ordinal, &error);
    if (!frame) {
      const auto stop = cancelled() ? ReplayResult::Stop::kCancelled
                                    : ReplayResult::Stop::kError;
      return stopped(std::move(result), stop, std::move(error));
    }
    const auto& control = request.frame.index->frames[ordinal].control;
    const FrameRect rect{control.x, control.y, control.width, control.height};
    std::vector<std::uint8_t> saved;
    if (control.dispose == 2 && ordinal != 0) {
      const std::uint64_t saved_bytes =
          static_cast<std::uint64_t>(rect.width) * rect.height * 4;
      if (saved_bytes > std::numeric_limits<std::size_t>::max()) {
        return stopped(std::move(result), ReplayResult::Stop::kPartial,
                       "previous rectangle is too large");
      }
      saved.resize(static_cast<std::size_t>(saved_bytes));
      for (std::uint32_t y = 0; y < rect.height; ++y) {
        const auto source_offset =
            (static_cast<std::size_t>(rect.y + y) * canvas.width + rect.x) * 4;
        const auto saved_offset = static_cast<std::size_t>(y) * rect.width * 4;
        std::copy_n(canvas.pixels.begin() + source_offset,
                    static_cast<std::size_t>(rect.width) * 4,
                    saved.begin() + saved_offset);
      }
    }
    const auto blend_result = pnga::png_reconstruction::blend_into(
        canvas, *frame, rect,
        control.blend == 0 ? Blend::kSource : Blend::kOver, cancelled);
    if (blend_result.cancelled || cancelled()) {
      return stopped(std::move(result), ReplayResult::Stop::kCancelled,
                     "animation replay cancelled");
    }
    if (!blend_result.success) {
      return stopped(std::move(result), ReplayResult::Stop::kError,
                     blend_result.error);
    }
    if (ordinal == last &&
        request.requested_stage == pnga::trace_model::Stage::kPostBlend) {
      break;
    }
    const auto dispose_result = pnga::png_reconstruction::dispose_into(
        canvas, rect,
        control.dispose == 0
            ? Dispose::kNone
            : control.dispose == 1 ? Dispose::kBackground : Dispose::kPrevious,
        saved, ordinal == 0, cancelled);
    if (dispose_result.cancelled || cancelled()) {
      return stopped(std::move(result), ReplayResult::Stop::kCancelled,
                     "animation replay cancelled");
    }
    if (!dispose_result.success) {
      return stopped(std::move(result), ReplayResult::Stop::kError,
                     dispose_result.error);
    }
  }

  auto image = std::make_shared<const RgbaImage>(std::move(canvas));
  if (!cache(key, image)) {
    return stopped(std::move(result), ReplayResult::Stop::kPartial,
                   "animation artifact exceeds the cache budget");
  }
  result.stop = ReplayResult::Stop::kReady;
  result.image = std::move(image);
  return result;
}

}  // namespace pnga::analysis_engine
