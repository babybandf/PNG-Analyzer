#include "pnga/png-format/virtual_frame_stream.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace pnga::png_format {

namespace {

struct Segment {
  std::uint64_t physical_offset = 0;
  std::uint64_t length = 0;
  std::uint64_t logical_start = 0;
};

bool is_type(const std::array<std::byte, 4>& actual, const char* expected) {
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != static_cast<std::byte>(expected[i])) {
      return false;
    }
  }
  return true;
}

bool valid_span(const pnga::io::IByteSource& source, std::uint64_t offset,
                std::uint64_t length) noexcept {
  return offset <= source.size() && length <= source.size() - offset;
}

bool append_segment(std::vector<Segment>& segments, std::uint64_t& total,
                    std::uint64_t physical_offset, std::uint64_t length) {
  if (length > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  if (length != 0) {
    segments.push_back(
        Segment{physical_offset, length, total});
  }
  total += length;
  return true;
}

class SegmentStream final : public IVirtualCompressedStream {
 public:
  SegmentStream(std::shared_ptr<const pnga::io::IByteSource> source,
                std::vector<Segment> segments, std::uint64_t total,
                std::shared_ptr<const AnimationIndex> index_owner = nullptr)
      : source_(std::move(source)),
        index_owner_(std::move(index_owner)),
        segments_(std::move(segments)),
        total_(total) {}

  std::uint64_t size() const noexcept override { return total_; }

  bool read(std::uint64_t logical_offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (out == nullptr && length != 0) {
      return false;
    }
    const std::uint64_t requested = static_cast<std::uint64_t>(length);
    if (logical_offset > total_ || requested > total_ - logical_offset) {
      return false;
    }
    if (length == 0) {
      return true;
    }

    std::uint64_t position = logical_offset;
    std::uint64_t remaining = requested;
    for (const auto& segment : segments_) {
      const std::uint64_t segment_end = segment.logical_start + segment.length;
      if (position >= segment_end) {
        continue;
      }
      if (position < segment.logical_start) {
        break;
      }
      const std::uint64_t in_segment = position - segment.logical_start;
      const std::uint64_t take = std::min(remaining, segment.length - in_segment);
      if (!source_->read(segment.physical_offset + in_segment, out,
                         static_cast<std::size_t>(take))) {
        return false;
      }
      out += static_cast<std::size_t>(take);
      position += take;
      remaining -= take;
      if (remaining == 0) {
        return true;
      }
    }
    return false;
  }

  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

  bool logical_to_physical(std::uint64_t logical_offset,
                           std::uint64_t length,
                           std::vector<PhysicalRange>& spans) const noexcept
      override {
    if (logical_offset > total_ || length > total_ - logical_offset) {
      return false;
    }
    std::vector<PhysicalRange> result;
    if (length == 0) {
      const auto anchor = zero_anchor(logical_offset);
      if (anchor.has_value()) {
        result.push_back(*anchor);
      }
      spans.insert(spans.end(), result.begin(), result.end());
      return true;
    }

    std::uint64_t position = logical_offset;
    std::uint64_t remaining = length;
    for (const auto& segment : segments_) {
      const std::uint64_t segment_end = segment.logical_start + segment.length;
      if (position >= segment_end) {
        continue;
      }
      if (position < segment.logical_start) {
        break;
      }
      const std::uint64_t in_segment = position - segment.logical_start;
      const std::uint64_t take = std::min(remaining, segment.length - in_segment);
      result.push_back(PhysicalRange{segment.physical_offset + in_segment, take});
      position += take;
      remaining -= take;
      if (remaining == 0) {
        spans.insert(spans.end(), result.begin(), result.end());
        return true;
      }
    }
    return false;
  }

  std::optional<std::uint64_t> physical_to_logical(
      std::uint64_t physical_offset) const noexcept override {
    for (std::size_t i = 0; i < segments_.size(); ++i) {
      const auto& segment = segments_[i];
      const std::uint64_t physical_end = segment.physical_offset + segment.length;
      if (physical_offset >= segment.physical_offset &&
          physical_offset < physical_end) {
        return segment.logical_start +
               (physical_offset - segment.physical_offset);
      }
      if (i + 1 == segments_.size() && physical_offset == physical_end) {
        return total_;
      }
    }
    return std::nullopt;
  }

 private:
  std::optional<PhysicalRange> zero_anchor(
      std::uint64_t logical_offset) const noexcept {
    if (segments_.empty()) {
      return std::nullopt;
    }
    for (const auto& segment : segments_) {
      const std::uint64_t segment_end = segment.logical_start + segment.length;
      if (logical_offset < segment_end) {
        return PhysicalRange{segment.physical_offset +
                                 (logical_offset - segment.logical_start),
                             0};
      }
      if (logical_offset == segment_end) {
        const auto next = &segment + 1;
        if (next != segments_.data() + segments_.size()) {
          return PhysicalRange{next->physical_offset, 0};
        }
        return PhysicalRange{segment.physical_offset + segment.length, 0};
      }
    }
    return std::nullopt;
  }

  std::shared_ptr<const pnga::io::IByteSource> source_;
  std::shared_ptr<const AnimationIndex> index_owner_;
  std::vector<Segment> segments_;
  std::uint64_t total_ = 0;
};

std::shared_ptr<const IVirtualCompressedStream> make_stream(
    std::shared_ptr<const pnga::io::IByteSource> source,
    std::vector<Segment> segments, std::uint64_t total,
    std::shared_ptr<const AnimationIndex> index_owner = nullptr) {
  if (!source) {
    return nullptr;
  }
  return std::make_shared<const SegmentStream>(std::move(source),
                                               std::move(segments), total,
                                               std::move(index_owner));
}

}  // namespace

std::shared_ptr<const IVirtualCompressedStream> make_frame_stream(
    std::shared_ptr<const pnga::io::IByteSource> source,
    std::shared_ptr<const AnimationIndex> index, std::uint32_t ordinal) {
  if (!source || !index || ordinal >= index->frames.size()) {
    return nullptr;
  }
  std::vector<Segment> segments;
  std::uint64_t total = 0;
  for (const auto& span : index->frames[ordinal].data) {
    if (!valid_span(*source, span.offset, span.length) ||
        !append_segment(segments, total, span.offset, span.length)) {
      return nullptr;
    }
  }
  return make_stream(std::move(source), std::move(segments), total,
                     std::move(index));
}

std::shared_ptr<const IVirtualCompressedStream> make_idat_stream(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const ChunkIndex& index) {
  if (!source) {
    return nullptr;
  }
  std::vector<Segment> segments;
  std::uint64_t total = 0;
  for (const auto& node : index.chunks) {
    if (!is_type(node.type, "IDAT")) {
      continue;
    }
    if (!valid_span(*source, node.data_offset, node.data_length) ||
        !append_segment(segments, total, node.data_offset, node.data_length)) {
      return nullptr;
    }
  }
  return make_stream(std::move(source), std::move(segments), total);
}

}  // namespace pnga::png_format
