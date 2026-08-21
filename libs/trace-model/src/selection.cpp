// WP-200 Selection model implementation: equality, merge and deterministic
// serialization. The serialization format is a compact token stream, e.g.
//   node:3;physical:8,4;logical:0,100;image:0,1,2,3,4,0;stage:filtered
// Every value is decimal; parsing is strict (no locale, no order dependence).

#include "pnga/trace_model/selection.h"

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
         std::to_string(c.y) + "," + std::to_string(c.channel));
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
      if (nums.size() != 6) {
        return std::nullopt;
      }
      ImageCoordinate c;
      auto frame = parse_u64(nums[0]);
      auto pass = parse_u64(nums[1]);
      auto row = parse_u64(nums[2]);
      auto x = parse_u64(nums[3]);
      auto y = parse_u64(nums[4]);
      auto channel = parse_u64(nums[5]);
      if (!frame || !pass || !row || !x || !y || !channel) {
        return std::nullopt;
      }
      c.frame = *frame;
      c.pass = *pass;
      c.row = *row;
      c.x = *x;
      c.y = *y;
      c.channel = *channel;
      out.image = c;
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
  return out;
}

}  // namespace pnga::trace_model
