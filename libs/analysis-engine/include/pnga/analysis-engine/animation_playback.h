#ifndef PNGA_ANALYSIS_ENGINE_ANIMATION_PLAYBACK_H
#define PNGA_ANALYSIS_ENGINE_ANIMATION_PLAYBACK_H

#include <pnga/png-format/animation_index.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

enum class PlaybackState {
  kPaused,
  kPlaying,
  kWaitingForFrame,
  kEnded,
  kPartial,
  kError,
};

enum class PlaybackSpeed { kQuarter, kHalf, kNormal, kDouble };

struct TimelineEntry {
  std::uint32_t ordinal = 0;
  std::uint16_t raw_num = 0;
  std::uint16_t raw_den = 0;
  std::uint64_t start_ns = 0;
  std::uint64_t duration_ns = 0;
};

struct AnimationTimeline {
  std::vector<TimelineEntry> entries;
  std::uint32_t num_plays = 0;
  bool complete = false;
};

std::optional<AnimationTimeline> make_timeline(
    const pnga::png_format::AnimationIndex& index, PlaybackSpeed speed);

class AnimationPlayback {
 public:
  AnimationPlayback(AnimationTimeline timeline, std::uint64_t generation);

  void play(std::uint64_t now_ns);
  void pause();
  bool seek(std::uint32_t ordinal, std::uint64_t now_ns);
  std::optional<std::uint32_t> tick(std::uint64_t now_ns);
  void frame_ready(std::uint32_t ordinal, std::uint64_t serial,
                   std::uint64_t now_ns);
  void set_speed(PlaybackSpeed speed, std::uint64_t now_ns);

  PlaybackState state() const noexcept { return state_; }
  std::uint32_t current_ordinal() const noexcept { return current_; }
  std::uint64_t request_serial() const noexcept { return serial_; }
  std::uint64_t generation() const noexcept { return generation_; }

 private:
  bool rebuild_durations();
  std::optional<std::uint64_t> duration_for(std::uint16_t raw_num,
                                            std::uint16_t raw_den) const;

  AnimationTimeline timeline_;
  PlaybackSpeed speed_ = PlaybackSpeed::kNormal;
  std::uint64_t generation_ = 0;
  PlaybackState state_ = PlaybackState::kPaused;
  std::uint32_t current_ = 0;
  std::uint32_t completed_plays_ = 0;
  std::uint64_t serial_ = 0;
  std::uint64_t display_start_ns_ = 0;
  bool frame_ready_ = false;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_ANIMATION_PLAYBACK_H
