#include <pnga/analysis-engine/animation_playback.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using pnga::analysis_engine::AnimationPlayback;
using pnga::analysis_engine::PlaybackSpeed;
using pnga::analysis_engine::PlaybackState;
using pnga::png_format::AnimationControl;
using pnga::png_format::AnimationIndex;
using pnga::png_format::AnimationStatus;
using pnga::png_format::FrameControl;
using pnga::analysis_engine::make_timeline;

namespace {

AnimationIndex complete_index(std::uint32_t plays = 1) {
  AnimationIndex index;
  index.status = AnimationStatus::kComplete;
  index.control = AnimationControl{2, plays};
  index.frames = {
      pnga::png_format::FrameRecord{0, FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
                                    false, {}},
      pnga::png_format::FrameRecord{1, FrameControl{1, 1, 1, 0, 0, 0, 0, 0, 0},
                                    false, {}}};
  return index;
}

}  // namespace

TEST_CASE("Timeline uses deterministic integer durations and minimum delay",
          "[analysis-engine][apng][wp704]") {
  const auto timeline = make_timeline(complete_index(), PlaybackSpeed::kNormal);
  REQUIRE(timeline.has_value());
  REQUIRE(timeline->complete);
  REQUIRE(timeline->entries.size() == 2);
  REQUIRE(timeline->entries[0].duration_ns == 10000000);
  REQUIRE(timeline->entries[0].raw_num == 1);
  REQUIRE(timeline->entries[0].raw_den == 100);

  const auto fast = make_timeline(complete_index(), PlaybackSpeed::kDouble);
  REQUIRE(fast.has_value());
  REQUIRE(fast->entries[0].duration_ns == 10000000);
}

TEST_CASE("Playback waits for matching frame readiness and ends finite loops",
          "[analysis-engine][apng][wp704]") {
  const auto timeline = make_timeline(complete_index(), PlaybackSpeed::kNormal);
  REQUIRE(timeline.has_value());
  AnimationPlayback playback(*timeline, 9);
  playback.play(0);
  REQUIRE(playback.state() == PlaybackState::kPlaying);
  REQUIRE(playback.tick(0) == 0);
  const auto serial = playback.request_serial();
  REQUIRE(serial != 0);
  REQUIRE(playback.state() == PlaybackState::kWaitingForFrame);
  playback.frame_ready(0, serial, 0);
  REQUIRE(playback.state() == PlaybackState::kPlaying);
  REQUIRE_FALSE(playback.tick(9999999).has_value());
  REQUIRE(playback.tick(10000000) == 1);
  const auto second_serial = playback.request_serial();
  playback.frame_ready(1, second_serial, 10000000);
  REQUIRE_FALSE(playback.tick(20000000).has_value());
  REQUIRE(playback.state() == PlaybackState::kEnded);
}

TEST_CASE("Seek is paused-only and speed resets display timing",
          "[analysis-engine][apng][wp704]") {
  const auto timeline = make_timeline(complete_index(0), PlaybackSpeed::kNormal);
  REQUIRE(timeline.has_value());
  AnimationPlayback playback(*timeline, 9);
  playback.play(0);
  REQUIRE_FALSE(playback.seek(1, 0));
  playback.pause();
  REQUIRE(playback.seek(1, 0));
  REQUIRE(playback.current_ordinal() == 1);
  playback.play(100);
  REQUIRE(playback.tick(100) == 1);
  const auto serial = playback.request_serial();
  playback.frame_ready(1, serial, 100);
  playback.set_speed(PlaybackSpeed::kDouble, 200);
  REQUIRE(playback.tick(10000000).has_value() == false);
}

TEST_CASE("Partial animation indexes cannot create a playable timeline",
          "[analysis-engine][apng][wp704]") {
  auto index = complete_index();
  index.status = AnimationStatus::kPartial;
  REQUIRE_FALSE(make_timeline(index, PlaybackSpeed::kNormal).has_value());
}
