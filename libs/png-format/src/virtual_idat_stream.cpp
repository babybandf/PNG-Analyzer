// WP-201 VirtualIDATStream. Segment building is O(#chunks) and copies no data;
// all logical reads stay bounded by the caller-provided buffer.

#include "pnga/png-format/virtual_idat_stream.h"

#include <algorithm>
#include <cstring>

namespace pnga::png_format {

namespace {

// Returns [offset, offset + length) clipped to [0, size); out-of-bounds ranges
// become empty. Used only on already-validated ranges, so no overflow.
void append_clipped(std::vector<PhysicalRange>& spans, std::uint64_t offset,
                    std::uint64_t length, std::uint64_t logical_start,
                    std::uint64_t seg_physical, std::uint64_t seg_length,
                    std::uint64_t seg_logical) noexcept {
  if (length == 0) {
    return;
  }
  spans.push_back(PhysicalRange{seg_physical + (offset - logical_start),
                                std::min(length, seg_length -
                                                     (offset - logical_start))});
}

}  // namespace

VirtualIDATStream::VirtualIDATStream(const ChunkIndex& index) {
  std::uint64_t logical = 0;
  for (const auto& node : index.chunks) {
    if (node.text() == "IDAT") {
      segments_.push_back(IdatSegment{node.data_offset, node.data_length,
                                      logical});
      logical += node.data_length;
    }
  }
  total_ = logical;
}

bool VirtualIDATStream::read(const pnga::io::IByteSource& source,
                             std::uint64_t logical_offset, std::byte* out,
                             std::size_t length) const noexcept {
  if (out == nullptr && length != 0) {
    return false;
  }
  if (length == 0) {
    return logical_offset <= total_;
  }
  if (logical_offset > total_ || length > total_ - logical_offset) {
    return false;
  }

  std::uint64_t pos = logical_offset;
  std::uint64_t remaining = static_cast<std::uint64_t>(length);
  for (const auto& seg : segments_) {
    if (remaining == 0) {
      break;
    }
    if (pos >= seg.logical_start + seg.length) {
      continue;  // this segment is entirely before the request
    }
    if (pos + remaining <= seg.logical_start) {
      break;  // this segment is entirely after the request
    }
    const std::uint64_t in_seg = pos >= seg.logical_start
                                     ? pos - seg.logical_start
                                     : 0;
    const std::uint64_t take =
        std::min(remaining, seg.length - in_seg);
    if (!source.read(seg.physical_offset + in_seg, out, take)) {
      return false;
    }
    out += take;
    pos += take;
    remaining -= take;
  }
  return remaining == 0;
}

bool VirtualIDATStream::logical_to_physical(
    std::uint64_t logical_offset, std::uint64_t length,
    std::vector<PhysicalRange>& spans) const noexcept {
  if (length == 0) {
    return logical_offset <= total_;
  }
  if (logical_offset > total_ || length > total_ - logical_offset) {
    return false;
  }
  std::uint64_t remaining = length;
  std::uint64_t pos = logical_offset;
  for (const auto& seg : segments_) {
    if (remaining == 0) {
      break;
    }
    if (pos >= seg.logical_start + seg.length) {
      continue;
    }
    if (pos + remaining <= seg.logical_start) {
      break;
    }
    const std::uint64_t in_seg = pos >= seg.logical_start
                                     ? pos - seg.logical_start
                                     : 0;
    const std::uint64_t take =
        std::min(remaining, seg.length - in_seg);
    spans.push_back(PhysicalRange{seg.physical_offset + in_seg, take});
    pos += take;
    remaining -= take;
  }
  return remaining == 0;
}

std::optional<std::uint64_t> VirtualIDATStream::physical_to_logical(
    std::uint64_t physical_offset) const noexcept {
  for (const auto& seg : segments_) {
    if (physical_offset >= seg.physical_offset &&
        physical_offset < seg.physical_offset + seg.length) {
      return seg.logical_start + (physical_offset - seg.physical_offset);
    }
  }
  return std::nullopt;
}

}  // namespace pnga::png_format
