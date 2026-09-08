#ifndef PNGA_PNG_FORMAT_ANIMATION_INDEX_H
#define PNGA_PNG_FORMAT_ANIMATION_INDEX_H

#include <pnga/io/byte_source.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pnga::png_format {

enum class AnimationStatus { kStatic, kComplete, kPartial, kInvalid, kCancelled };
enum class AnimationStop { kNone, kFormat, kBudget, kCancelled };

struct AnimationControl {
  std::uint32_t num_frames = 0;
  std::uint32_t num_plays = 0;
};

struct FrameControl {
  std::uint32_t sequence = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint16_t delay_num = 0;
  std::uint16_t delay_den = 0;
  std::uint8_t dispose = 0;
  std::uint8_t blend = 0;
};

struct FrameDataSpan {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
};

struct FrameRecord {
  std::uint32_t ordinal = 0;
  FrameControl control;
  bool uses_idat = false;
  std::vector<FrameDataSpan> data;
};

struct AnimationIssue {
  std::string rule_id;
  std::uint64_t offset = 0;
};

struct AnimationLimits {
  std::uint64_t max_frames = 100000;
  std::uint64_t max_animation_chunks = 1000000;
  std::uint64_t max_metadata_bytes = 64ull * 1024 * 1024;
};

struct AnimationIndex {
  AnimationStatus status = AnimationStatus::kStatic;
  AnimationStop stop = AnimationStop::kNone;
  std::optional<AnimationControl> control;
  std::uint32_t canvas_width = 0;
  std::uint32_t canvas_height = 0;
  bool default_is_frame = false;
  std::vector<FrameRecord> frames;
  std::vector<AnimationIssue> issues;
  std::uint64_t retained_bytes = 0;
};

AnimationIndex index_animation(
    const pnga::io::IByteSource& source, const AnimationLimits& limits,
    const std::function<bool()>& cancelled);

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_ANIMATION_INDEX_H
