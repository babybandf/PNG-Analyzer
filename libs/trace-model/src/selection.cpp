// WP-200 Selection model implementation: equality, merge and deterministic
// serialization. The serialization format is a compact token stream, e.g.
//   node:3;physical:8,4;logical:0,100;image:0,1,2,3,4;channel:0;stage:filtered
// Every value is decimal; parsing is strict (no locale, no order dependence).

#include "pnga/trace-model/selection.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pnga::trace_model {

namespace {

const char* kStageNames[] = {
    "file", "chunk", "filtered", "unfiltered", "native", "delivered",
    "trace", "unknown",
};

std::optional<std::uint64_t> parse_u64(std::string_view token) {
  if (token.empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  const auto* end = token.data() + token.size();
  const auto res = std::from_chars(token.data(), end, value);
  if (res.ec != std::errc{} || res.ptr != end) {
    return std::nullopt;
  }
  return value;
}

// Splits `text` on `delim` into non-empty tokens.
std::vector<std::string_view> split(std::string_view text, char delim) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find(delim, start);
    if (end == std::string_view::npos) {
      out.push_back(text.substr(start));
      break;
    }
    if (end > start) {
      out.push_back(text.substr(start, end - start));
    }
    start = end + 1;
  }
  return out;
}

}  // namespace

bool ImageCoordinate::valid() const noexcept {
  if (pass > 7) {
    return false;
  }
  if (sample_byte.has_value() && *sample_byte > 1) {
    return false;
  }
  if (sample_byte.has_value() && packed_sample.has_value()) {
    return false;
  }
  if (packed_sample.has_value()) {
    const auto packed = *packed_sample;
    if (packed.bit_length == 0 || packed.bit_length > 8 ||
        packed.bit_offset >= 8 ||
        static_cast<unsigned>(packed.bit_offset) + packed.bit_length > 8 ||
        !channel.has_value()) {
      return false;
    }
  }
  if (sample_byte.has_value() && !channel.has_value()) {
    return false;
  }
  return true;
}

const char* stage_text(Stage stage) noexcept {
  const std::size_t i = static_cast<std::size_t>(stage);
  return i < sizeof(kStageNames) / sizeof(kStageNames[0]) ? kStageNames[i]
                                                          : "unknown";
}

bool stage_from_text(std::string_view text, Stage& out) noexcept {
  for (std::size_t i = 0; i < sizeof(kStageNames) / sizeof(kStageNames[0]);
       ++i) {
    if (text == kStageNames[i]) {
      out = static_cast<Stage>(i);
      return true;
    }
  }
  return false;
}

bool Selection::empty() const noexcept {
  return !node.has_value() && physical_spans.empty() && !logical.has_value() &&
         !image.has_value() && stage == Stage::kUnknown;
}

void Selection::merge_with(const Selection& other) noexcept {
  if (other.node.has_value()) {
    node = other.node;
  }
  if (!other.physical_spans.empty()) {
    physical_spans = other.physical_spans;
  }
  if (other.logical.has_value()) {
    logical = other.logical;
  }
  if (other.image.has_value()) {
    image = other.image;
  }
  if (other.stage != Stage::kUnknown) {
    stage = other.stage;
  }
}

Selection Selection::merged_with(const Selection& other) const noexcept {
  Selection out = *this;
  out.merge_with(other);
  return out;
}

std::string serialize(const Selection& selection) {
  std::ostringstream out;
  bool first = true;
  const auto emit = [&](const std::string& token) {
    if (!first) {
      out << ';';
    }
    out << token;
    first = false;
  };

  if (selection.node.has_value()) {
    emit("node:" + std::to_string(*selection.node));
  }
  for (const auto& span : selection.physical_spans) {
    if (span.bit_aligned) {
      emit("physical_bit:" + std::to_string(span.offset) + "," +
           std::to_string(span.length) + "," + std::to_string(span.bit_offset));
    } else {
      emit("physical:" + std::to_string(span.offset) + "," +
           std::to_string(span.length));
    }
  }
  if (selection.logical.has_value()) {
    emit("logical:" + std::to_string(selection.logical->start) + "," +
         std::to_string(selection.logical->length));
  }
  if (selection.image.has_value()) {
    const auto& c = *selection.image;
    emit("image:" + std::to_string(c.frame) + "," + std::to_string(c.pass) +
         "," + std::to_string(c.row) + "," + std::to_string(c.x) + "," +
         std::to_string(c.y));
    if (c.channel.has_value()) {
      emit("channel:" + std::to_string(*c.channel));
    }
    if (c.sample_byte.has_value()) {
      emit("sample_byte:" + std::to_string(*c.sample_byte));
    }
    if (c.packed_sample.has_value()) {
      emit("packed_sample:" +
           std::to_string(c.packed_sample->bit_offset) + "," +
           std::to_string(c.packed_sample->bit_length));
    }
  }
  if (selection.stage != Stage::kUnknown) {
    emit(std::string("stage:") + stage_text(selection.stage));
  }
  return out.str();
}

std::optional<Selection> deserialize(std::string_view text) {
  Selection out;
  if (text.empty()) {
    return out;
  }
  bool image_seen = false;
  bool channel_seen = false;
  bool sample_byte_seen = false;
  bool packed_sample_seen = false;
  std::optional<std::uint64_t> pending_channel;
  std::optional<std::uint8_t> pending_sample_byte;
  std::optional<PackedSampleCoordinate> pending_packed_sample;
  for (const auto token : split(text, ';')) {
    const std::size_t colon = token.find(':');
    if (colon == std::string_view::npos || colon == 0) {
      return std::nullopt;
    }
    const std::string_view key = token.substr(0, colon);
    const std::string_view value = token.substr(colon + 1);
    const auto nums = split(value, ',');

    if (key == "node") {
      if (nums.size() != 1) {
        return std::nullopt;
      }
      auto v = parse_u64(nums[0]);
      if (!v.has_value()) {
        return std::nullopt;
      }
      out.node = *v;
    } else if (key == "physical" || key == "physical_bit") {
      if (nums.size() != (key == "physical" ? 2u : 3u)) {
        return std::nullopt;
      }
      auto off = parse_u64(nums[0]);
      auto len = parse_u64(nums[1]);
      if (!off.has_value() || !len.has_value()) {
        return std::nullopt;
      }
      BitSpan span;
      span.offset = *off;
      span.length = *len;
      if (key == "physical_bit") {
        auto bit = parse_u64(nums[2]);
        if (!bit.has_value() || *bit > 7) {
          return std::nullopt;
        }
        span.bit_offset = static_cast<std::uint8_t>(*bit);
        span.bit_aligned = true;
      }
      out.physical_spans.push_back(span);
    } else if (key == "logical") {
      if (nums.size() != 2) {
        return std::nullopt;
      }
      auto start = parse_u64(nums[0]);
      auto len = parse_u64(nums[1]);
      if (!start.has_value() || !len.has_value()) {
        return std::nullopt;
      }
      out.logical = StreamSpan{*start, *len};
    } else if (key == "image") {
      if (image_seen || (nums.size() != 5 && nums.size() != 6)) {
        return std::nullopt;
      }
      ImageCoordinate c;
      auto frame = parse_u64(nums[0]);
      auto pass = parse_u64(nums[1]);
      auto row = parse_u64(nums[2]);
      auto x = parse_u64(nums[3]);
      auto y = parse_u64(nums[4]);
      if (!frame || !pass || !row || !x || !y) {
        return std::nullopt;
      }
      c.frame = *frame;
      c.pass = *pass;
      c.row = *row;
      c.x = *x;
      c.y = *y;
      if (nums.size() == 6) {
        if (channel_seen) {
          return std::nullopt;
        }
        auto channel = parse_u64(nums[5]);
        if (!channel) {
          return std::nullopt;
        }
        c.channel = *channel;
        channel_seen = true;
      }
      if (pending_channel.has_value()) {
        c.channel = pending_channel;
      }
      if (pending_sample_byte.has_value()) {
        c.sample_byte = pending_sample_byte;
      }
      if (pending_packed_sample.has_value()) {
        c.packed_sample = pending_packed_sample;
      }
      out.image = c;
      image_seen = true;
    } else if (key == "channel") {
      if (channel_seen || nums.size() != 1) {
        return std::nullopt;
      }
      auto channel = parse_u64(nums[0]);
      if (!channel) {
        return std::nullopt;
      }
      if (out.image.has_value()) {
        out.image->channel = *channel;
      } else {
        pending_channel = *channel;
      }
      channel_seen = true;
    } else if (key == "sample_byte") {
      if (sample_byte_seen || nums.size() != 1) {
        return std::nullopt;
      }
      auto sample_byte = parse_u64(nums[0]);
      if (!sample_byte || *sample_byte > 1) {
        return std::nullopt;
      }
      if (out.image.has_value()) {
        out.image->sample_byte = static_cast<std::uint8_t>(*sample_byte);
      } else {
        pending_sample_byte = static_cast<std::uint8_t>(*sample_byte);
      }
      sample_byte_seen = true;
    } else if (key == "packed_sample") {
      if (packed_sample_seen || nums.size() != 2) {
        return std::nullopt;
      }
      auto bit_offset = parse_u64(nums[0]);
      auto bit_length = parse_u64(nums[1]);
      if (!bit_offset || !bit_length || *bit_offset >= 8 ||
          *bit_length == 0 || *bit_length > 8 ||
          *bit_offset + *bit_length > 8) {
        return std::nullopt;
      }
      const PackedSampleCoordinate packed{
          static_cast<std::uint8_t>(*bit_offset),
          static_cast<std::uint8_t>(*bit_length)};
      if (out.image.has_value()) {
        out.image->packed_sample = packed;
      } else {
        pending_packed_sample = packed;
      }
      packed_sample_seen = true;
    } else if (key == "stage") {
      if (nums.size() != 1) {
        return std::nullopt;
      }
      if (!stage_from_text(value, out.stage)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;  // unknown key
    }
  }
  if ((channel_seen || sample_byte_seen || packed_sample_seen) &&
      !image_seen) {
    return std::nullopt;
  }
  return out;
}

}  // namespace pnga::trace_model
