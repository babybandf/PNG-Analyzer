#include "pnga/png-format/animation_index.h"

#include "pnga/png-format/chunk_index.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pnga::png_format {

namespace {

constexpr std::uint64_t kChunkHeaderBytes = 8;
constexpr std::uint64_t kChunkCrcBytes = 4;

bool type_is(const std::array<std::byte, 4>& type, const char* expected) {
  for (std::size_t i = 0; i < 4; ++i) {
    if (type[i] != static_cast<std::byte>(expected[i])) {
      return false;
    }
  }
  return true;
}

std::uint32_t u32(const std::byte* data) noexcept {
  return (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[0]))
          << 24) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[1]))
          << 16) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[2]))
          << 8) |
         static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[3]));
}

std::uint16_t u16(const std::byte* data) noexcept {
  return static_cast<std::uint16_t>(
      (std::to_integer<unsigned int>(data[0]) << 8) |
      std::to_integer<unsigned int>(data[1]));
}

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *result = left + right;
  return true;
}

bool account(AnimationIndex& index, const AnimationLimits& limits,
             std::uint64_t bytes) {
  if (index.retained_bytes > limits.max_metadata_bytes ||
      bytes > limits.max_metadata_bytes - index.retained_bytes) {
    return false;
  }
  index.retained_bytes += bytes;
  return true;
}

bool grow_frames(AnimationIndex& index, const AnimationLimits& limits) {
  if (index.frames.size() < index.frames.capacity()) {
    return true;
  }
  const std::uint64_t old_capacity = index.frames.capacity();
  std::uint64_t new_capacity = old_capacity == 0 ? 1 : old_capacity * 2;
  if (new_capacity < old_capacity || new_capacity > limits.max_frames) {
    new_capacity = limits.max_frames;
  }
  if (new_capacity <= old_capacity ||
      new_capacity - old_capacity >
          std::numeric_limits<std::uint64_t>::max() /
              sizeof(FrameRecord) ||
      !account(index, limits,
               (new_capacity - old_capacity) * sizeof(FrameRecord))) {
    return false;
  }
  index.frames.reserve(static_cast<std::size_t>(new_capacity));
  return true;
}

bool grow_data(AnimationIndex& index, FrameRecord& frame,
               const AnimationLimits& limits) {
  if (frame.data.size() < frame.data.capacity()) {
    return true;
  }
  const std::uint64_t old_capacity = frame.data.capacity();
  std::uint64_t new_capacity = old_capacity == 0 ? 1 : old_capacity * 2;
  if (new_capacity < old_capacity ||
      new_capacity - old_capacity >
          std::numeric_limits<std::uint64_t>::max() /
              sizeof(FrameDataSpan) ||
      !account(index, limits,
               (new_capacity - old_capacity) * sizeof(FrameDataSpan))) {
    return false;
  }
  frame.data.reserve(static_cast<std::size_t>(new_capacity));
  return true;
}

bool has_payload(const FrameRecord& frame) {
  for (const auto& span : frame.data) {
    if (span.length != 0) {
      return true;
    }
  }
  return false;
}

void discard_current(AnimationIndex& index, FrameRecord* current) {
  if (current != nullptr &&
      !index.frames.empty() && &index.frames.back() == current) {
    index.frames.pop_back();
  }
}

bool append_issue(AnimationIndex& index, const AnimationLimits& limits,
                  const char* rule_id, std::uint64_t offset) {
  const std::size_t rule_size = std::char_traits<char>::length(rule_id);
  if (!account(index, limits,
               sizeof(AnimationIssue) + static_cast<std::uint64_t>(rule_size) +
                   1)) {
    return false;
  }
  index.issues.push_back(AnimationIssue{rule_id, offset});
  return true;
}

bool stop_format(AnimationIndex& index, const AnimationLimits& limits,
                 const char* rule_id, std::uint64_t offset) {
  if (!append_issue(index, limits, rule_id, offset)) {
    index.status = AnimationStatus::kPartial;
    index.stop = AnimationStop::kBudget;
    return false;
  }
  index.status = index.frames.empty() ? AnimationStatus::kInvalid
                                      : AnimationStatus::kPartial;
  index.stop = AnimationStop::kFormat;
  return false;
}

bool append_span(AnimationIndex& index, const AnimationLimits& limits,
                 FrameRecord& frame, std::uint64_t offset,
                 std::uint64_t length) {
  if (!grow_data(index, frame, limits)) {
    index.status = AnimationStatus::kPartial;
    index.stop = AnimationStop::kBudget;
    return false;
  }
  frame.data.push_back(FrameDataSpan{offset, length});
  return true;
}

bool check_sequence(AnimationIndex& index, const AnimationLimits& limits,
                    std::uint32_t sequence, std::uint64_t offset,
                    std::uint32_t* expected, bool* exhausted) {
  if (*exhausted || sequence != *expected) {
    stop_format(index, limits, "apng.sequence", offset);
    return false;
  }
  if (sequence == std::numeric_limits<std::uint32_t>::max()) {
    *exhausted = true;
  } else {
    ++*expected;
  }
  return true;
}

}  // namespace

AnimationIndex index_animation(
    const pnga::io::IByteSource& source, const AnimationLimits& limits,
    const std::function<bool()>& cancelled) {
  AnimationIndex index;
  if (limits.max_frames == 0 || limits.max_animation_chunks == 0 ||
      limits.max_metadata_bytes < sizeof(AnimationIndex) ||
      !account(index, limits, sizeof(AnimationIndex))) {
    index.status = AnimationStatus::kPartial;
    index.stop = AnimationStop::kBudget;
    return index;
  }

  const std::uint64_t file_size = source.size();
  if (file_size < kPngSignature.size()) {
    stop_format(index, limits, "apng.envelope", file_size);
    return index;
  }
  const auto signature = source.view(0, kPngSignature.size());
  if (!signature.has_value() ||
      !std::equal(kPngSignature.begin(), kPngSignature.end(),
                  signature->data)) {
    stop_format(index, limits, "apng.envelope", 0);
    return index;
  }

  bool saw_actl = false;
  bool saw_idat = false;
  bool saw_fctl = false;
  bool saw_iend = false;
  bool canvas_known = false;
  std::uint32_t canvas_width = 0;
  std::uint32_t canvas_height = 0;
  std::uint64_t animation_chunk_count = 0;
  std::uint32_t expected_sequence = 0;
  bool sequence_exhausted = false;
  FrameRecord* current = nullptr;
  std::uint64_t pos = kPngSignature.size();

  while (pos < file_size) {
    if (cancelled && cancelled()) {
      index.status = AnimationStatus::kCancelled;
      index.stop = AnimationStop::kCancelled;
      return index;
    }
    if (file_size - pos < kChunkHeaderBytes) {
      stop_format(index, limits, "apng.envelope", pos);
      return index;
    }
    const auto header = source.view(pos, kChunkHeaderBytes);
    if (!header.has_value()) {
      stop_format(index, limits, "apng.envelope", pos);
      return index;
    }
    const std::uint64_t data_offset = pos + kChunkHeaderBytes;
    const std::uint64_t length = u32(header->data);
    if (length > file_size - data_offset) {
      stop_format(index, limits, "apng.envelope", data_offset);
      return index;
    }
    const std::uint64_t crc_offset = data_offset + length;
    if (kChunkCrcBytes > file_size - crc_offset) {
      stop_format(index, limits, "apng.envelope", crc_offset);
      return index;
    }
    std::array<std::byte, 4> type{};
    std::copy(header->data + 4, header->data + kChunkHeaderBytes, type.begin());

    const bool is_animation_chunk =
        type_is(type, "acTL") || type_is(type, "fcTL") || type_is(type, "fdAT");
    if (is_animation_chunk) {
      if (animation_chunk_count >= limits.max_animation_chunks) {
        index.status = AnimationStatus::kPartial;
        index.stop = AnimationStop::kBudget;
        return index;
      }
      ++animation_chunk_count;
    }

    if (type_is(type, "IHDR")) {
      if (length != 13) {
        stop_format(index, limits, "apng.frame.geometry", pos);
        return index;
      }
      const auto data = source.view(data_offset, 13);
      if (!data.has_value()) {
        stop_format(index, limits, "apng.envelope", data_offset);
        return index;
      }
      canvas_width = u32(data->data);
      canvas_height = u32(data->data + 4);
      index.canvas_width = canvas_width;
      index.canvas_height = canvas_height;
      canvas_known = true;
    } else if (type_is(type, "acTL")) {
      if (saw_actl || saw_idat || length != 8) {
        stop_format(index, limits, "apng.actl.order", pos);
        return index;
      }
      const auto data = source.view(data_offset, 8);
      if (!data.has_value()) {
        stop_format(index, limits, "apng.envelope", data_offset);
        return index;
      }
      index.control = AnimationControl{u32(data->data), u32(data->data + 4)};
      saw_actl = true;
      if (index.control->num_frames == 0) {
        stop_format(index, limits, "apng.actl.count", pos);
        return index;
      }
    } else if (type_is(type, "fcTL")) {
      if (!saw_actl || length != 26) {
        stop_format(index, limits, "apng.frame.control", pos);
        return index;
      }
      const auto data = source.view(data_offset, 26);
      if (!data.has_value()) {
        stop_format(index, limits, "apng.envelope", data_offset);
        return index;
      }
      const FrameControl control{
          u32(data->data),       u32(data->data + 4), u32(data->data + 8),
          u32(data->data + 12),  u32(data->data + 16), u16(data->data + 20),
          u16(data->data + 22),  std::to_integer<std::uint8_t>(data->data[24]),
          std::to_integer<std::uint8_t>(data->data[25])};
      if (current != nullptr && !has_payload(*current)) {
        discard_current(index, current);
        current = nullptr;
        stop_format(index, limits, "apng.frame.data", pos);
        return index;
      }
      if (!check_sequence(index, limits, control.sequence, pos,
                          &expected_sequence, &sequence_exhausted)) {
        return index;
      }
      if (index.frames.size() >= limits.max_frames) {
        index.status = AnimationStatus::kPartial;
        index.stop = AnimationStop::kBudget;
        return index;
      }
      if (!canvas_known || control.width == 0 || control.height == 0 ||
          control.x > canvas_width || control.y > canvas_height ||
          control.width > canvas_width - control.x ||
          control.height > canvas_height - control.y) {
        stop_format(index, limits, "apng.frame.geometry", pos);
        return index;
      }
      if (!saw_idat && index.frames.empty() &&
          (control.x != 0 || control.y != 0 || control.width != canvas_width ||
           control.height != canvas_height)) {
        stop_format(index, limits, "apng.frame.geometry", pos);
        return index;
      }
      if (!grow_frames(index, limits)) {
        index.status = AnimationStatus::kPartial;
        index.stop = AnimationStop::kBudget;
        return index;
      }
      index.frames.push_back(
          FrameRecord{static_cast<std::uint32_t>(index.frames.size()), control,
                      false, {}});
      current = &index.frames.back();
      saw_fctl = true;
      if (!saw_idat && index.frames.size() == 1) {
        index.default_is_frame = true;
      }
    } else if (type_is(type, "IDAT")) {
      saw_idat = true;
      if (saw_actl && saw_fctl && index.default_is_frame && current != nullptr &&
          index.frames.size() == 1) {
        current->uses_idat = true;
        if (!append_span(index, limits, *current, data_offset, length)) {
          return index;
        }
      }
    } else if (type_is(type, "fdAT")) {
      if (!saw_actl || !saw_fctl || current == nullptr || length < 4) {
        discard_current(index, current);
        current = nullptr;
        stop_format(index, limits, "apng.frame.data", pos);
        return index;
      }
      const auto data = source.view(data_offset, 4);
      if (!data.has_value()) {
        discard_current(index, current);
        current = nullptr;
        stop_format(index, limits, "apng.envelope", data_offset);
        return index;
      }
      if (!check_sequence(index, limits, u32(data->data), pos,
                          &expected_sequence, &sequence_exhausted)) {
        discard_current(index, current);
        current = nullptr;
        return index;
      }
      if (!append_span(index, limits, *current, data_offset + 4, length - 4)) {
        discard_current(index, current);
        current = nullptr;
        return index;
      }
    } else if (type_is(type, "IEND")) {
      if (length != 0) {
        stop_format(index, limits, "apng.envelope", pos);
        return index;
      }
      saw_iend = true;
      const std::uint64_t end = crc_offset + kChunkCrcBytes;
      if (end != file_size) {
        stop_format(index, limits, "apng.envelope", end);
        return index;
      }
      break;
    }

    pos = crc_offset + kChunkCrcBytes;
  }

  if (!saw_iend) {
    if (current != nullptr) {
      discard_current(index, current);
      current = nullptr;
    }
    stop_format(index, limits, "apng.envelope", pos);
    return index;
  }
  if (!saw_actl) {
    index.status = AnimationStatus::kStatic;
    index.stop = AnimationStop::kNone;
    return index;
  }
  if (!index.control.has_value() ||
      index.frames.size() != index.control->num_frames) {
    stop_format(index, limits, "apng.frame.count", file_size);
    return index;
  }
  if (current != nullptr && !has_payload(*current)) {
    discard_current(index, current);
    current = nullptr;
    stop_format(index, limits, "apng.frame.data", file_size);
    return index;
  }
  for (const auto& frame : index.frames) {
    std::uint64_t payload = 0;
    for (const auto& span : frame.data) {
      if (!checked_add(payload, span.length, &payload)) {
        stop_format(index, limits, "apng.frame.data", span.offset);
        return index;
      }
    }
    if (payload == 0) {
      stop_format(index, limits, "apng.frame.data", file_size);
      return index;
    }
  }
  index.status = AnimationStatus::kComplete;
  index.stop = AnimationStop::kNone;
  return index;
}

}  // namespace pnga::png_format
