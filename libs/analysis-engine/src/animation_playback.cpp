#include "pnga/analysis-engine/animation_playback.h"

#include <limits>
#include <utility>

namespace pnga::analysis_engine {

namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ull;
constexpr std::uint64_t kMinimumDuration = 10000000ull;

std::optional<std::uint64_t> checked_add(std::uint64_t a,
                                         std::uint64_t b) noexcept {
  if (a > std::numeric_limits<std::uint64_t>::max() - b) {
    return std::nullopt;
  }
  return a + b;
}

std::optional<std::uint64_t> checked_mul(std::uint64_t a,
                                         std::uint64_t b) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
    return std::nullopt;
  }
  return a * b;
}

}  // namespace

std::optional<AnimationTimeline> make_timeline(
    const pnga::png_format::AnimationIndex& index, PlaybackSpeed speed) {
  if (index.status != pnga::png_format::AnimationStatus::kComplete ||
      !index.control.has_value() || index.frames.empty() ||
      index.frames.size() != index.control->num_frames) {
    return std::nullopt;
  }
  AnimationTimeline timeline;
  timeline.num_plays = index.control->num_plays;
  timeline.complete = true;
  timeline.entries.reserve(index.frames.size());
  std::uint64_t start = 0;
  for (std::size_t i = 0; i < index.frames.size(); ++i) {
    const auto& control = index.frames[i].control;
    const std::uint64_t den = control.delay_den == 0 ? 100 : control.delay_den;
    const auto scaled = checked_mul(control.delay_num, kNanosecondsPerSecond);
    if (!scaled.has_value()) {
      return std::nullopt;
    }
    std::uint64_t duration = (*scaled + den - 1) / den;
    switch (speed) {
      case PlaybackSpeed::kQuarter:
        if (duration > std::numeric_limits<std::uint64_t>::max() / 4) {
          return std::nullopt;
        }
        duration *= 4;
        break;
      case PlaybackSpeed::kHalf:
        if (duration > std::numeric_limits<std::uint64_t>::max() / 2) {
          return std::nullopt;
        }
        duration *= 2;
        break;
      case PlaybackSpeed::kNormal:
        break;
      case PlaybackSpeed::kDouble:
        duration = (duration + 1) / 2;
        break;
    }
    duration = duration < kMinimumDuration ? kMinimumDuration : duration;
    timeline.entries.push_back(TimelineEntry{
        static_cast<std::uint32_t>(i), control.delay_num, control.delay_den,
        start, duration});
    const auto next = checked_add(start, duration);
    if (!next.has_value()) {
      return std::nullopt;
    }
    start = *next;
  }
  return timeline;
}

AnimationPlayback::AnimationPlayback(AnimationTimeline timeline,
                                     std::uint64_t generation)
    : timeline_(std::move(timeline)), generation_(generation) {}

std::optional<std::uint64_t> AnimationPlayback::duration_for(
    std::uint16_t raw_num, std::uint16_t raw_den) const {
  const std::uint64_t den = raw_den == 0 ? 100 : raw_den;
  const auto scaled = checked_mul(raw_num, kNanosecondsPerSecond);
  if (!scaled.has_value() || *scaled > std::numeric_limits<std::uint64_t>::max() -
                                  (den - 1)) {
    return std::nullopt;
  }
  std::uint64_t duration = (*scaled + den - 1) / den;
  switch (speed_) {
    case PlaybackSpeed::kQuarter:
      if (duration > std::numeric_limits<std::uint64_t>::max() / 4) {
        return std::nullopt;
      }
      duration *= 4;
      break;
    case PlaybackSpeed::kHalf:
      if (duration > std::numeric_limits<std::uint64_t>::max() / 2) {
        return std::nullopt;
      }
      duration *= 2;
      break;
    case PlaybackSpeed::kNormal:
      break;
    case PlaybackSpeed::kDouble:
      if (duration == std::numeric_limits<std::uint64_t>::max()) {
        duration = (duration / 2) + 1;
      } else {
        duration = (duration + 1) / 2;
      }
      break;
  }
  return duration < kMinimumDuration ? kMinimumDuration : duration;
}

bool AnimationPlayback::rebuild_durations() {
  std::uint64_t start = 0;
  for (auto& entry : timeline_.entries) {
    const auto duration = duration_for(entry.raw_num, entry.raw_den);
    if (!duration.has_value()) {
      return false;
    }
    entry.start_ns = start;
    entry.duration_ns = *duration;
    const auto next = checked_add(start, *duration);
    if (!next.has_value()) {
      return false;
    }
    start = *next;
  }
  return true;
}

void AnimationPlayback::play(std::uint64_t now_ns) {
  if (timeline_.entries.empty()) {
    state_ = PlaybackState::kError;
    return;
  }
  if (state_ == PlaybackState::kEnded) {
    current_ = 0;
    completed_plays_ = 0;
    serial_ = 0;
    frame_ready_ = false;
  }
  if (state_ == PlaybackState::kPaused || state_ == PlaybackState::kEnded) {
    display_start_ns_ = now_ns;
    state_ = PlaybackState::kPlaying;
  }
}

void AnimationPlayback::pause() {
  if (state_ == PlaybackState::kPlaying ||
      state_ == PlaybackState::kWaitingForFrame) {
    state_ = PlaybackState::kPaused;
    frame_ready_ = false;
  }
}

bool AnimationPlayback::seek(std::uint32_t ordinal, std::uint64_t now_ns) {
  if (state_ != PlaybackState::kPaused || ordinal >= timeline_.entries.size()) {
    return false;
  }
  current_ = ordinal;
  display_start_ns_ = now_ns;
  frame_ready_ = false;
  serial_ = 0;
  return true;
}

std::optional<std::uint32_t> AnimationPlayback::tick(std::uint64_t now_ns) {
  if (state_ != PlaybackState::kPlaying || timeline_.entries.empty()) {
    return std::nullopt;
  }
  if (!frame_ready_) {
    ++serial_;
    state_ = PlaybackState::kWaitingForFrame;
    return current_;
  }
  const auto& entry = timeline_.entries[current_];
  if (now_ns < display_start_ns_ ||
      now_ns - display_start_ns_ < entry.duration_ns) {
    return std::nullopt;
  }
  if (current_ + 1 < timeline_.entries.size()) {
    ++current_;
  } else if (timeline_.num_plays == 0 ||
             completed_plays_ < timeline_.num_plays - 1) {
    if (completed_plays_ != std::numeric_limits<std::uint32_t>::max()) {
      ++completed_plays_;
    }
    current_ = 0;
  } else {
    state_ = PlaybackState::kEnded;
    return std::nullopt;
  }
  frame_ready_ = false;
  ++serial_;
  state_ = PlaybackState::kWaitingForFrame;
  return current_;
}

void AnimationPlayback::frame_ready(std::uint32_t ordinal,
                                    std::uint64_t serial,
                                    std::uint64_t now_ns) {
  if (state_ != PlaybackState::kWaitingForFrame || ordinal != current_ ||
      serial != serial_) {
    return;
  }
  frame_ready_ = true;
  display_start_ns_ = now_ns;
  state_ = PlaybackState::kPlaying;
}

void AnimationPlayback::set_speed(PlaybackSpeed speed, std::uint64_t now_ns) {
  speed_ = speed;
  if (!rebuild_durations()) {
    state_ = PlaybackState::kError;
    return;
  }
  if (state_ == PlaybackState::kPlaying) {
    display_start_ns_ = now_ns;
  }
}

}  // namespace pnga::analysis_engine
