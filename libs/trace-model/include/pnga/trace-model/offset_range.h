#ifndef PNGA_TRACE_MODEL_OFFSET_RANGE_H
#define PNGA_TRACE_MODEL_OFFSET_RANGE_H

// WP-5U12 P0-A: explicit coordinate domains for compression provenance.
// Ranges are half-open. The domain is part of the type so a zlib-stream bit
// range cannot be passed to an API that expects a file or inflated-byte range.

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace pnga::trace_model {

enum class OffsetDomain {
  kFile,
  kZlibStream,
  kDeflatePayload,
  kInflated,
};

enum class OffsetUnit { kBytes, kBits };

template <OffsetDomain Domain, OffsetUnit Unit>
struct Offset {
  std::uint64_t value = 0;

  constexpr Offset() noexcept = default;
  constexpr explicit Offset(std::uint64_t raw) noexcept : value(raw) {}
  constexpr std::uint64_t raw_value() const noexcept { return value; }

  constexpr bool operator==(const Offset&) const noexcept = default;
  constexpr auto operator<=>(const Offset&) const noexcept = default;

  constexpr Offset operator+(std::uint64_t delta) const noexcept {
    return Offset{value + delta};
  }
  constexpr Offset operator-(std::uint64_t delta) const noexcept {
    return Offset{value - delta};
  }
  constexpr std::uint64_t operator-(const Offset& other) const noexcept {
    return value - other.value;
  }
};

template <OffsetDomain Domain, OffsetUnit Unit>
struct Range {
  using offset_type = Offset<Domain, Unit>;

  offset_type begin{};
  offset_type end{};

  constexpr bool operator==(const Range&) const noexcept = default;
  constexpr bool valid() const noexcept { return begin <= end; }
  constexpr bool empty() const noexcept { return begin == end; }
  constexpr bool contains(offset_type value) const noexcept {
    return begin <= value && value < end;
  }
  constexpr bool overlaps(const Range& other) const noexcept {
    return begin < other.end && other.begin < end;
  }
};

using FileByteOffset = Offset<OffsetDomain::kFile, OffsetUnit::kBytes>;
using ZlibByteOffset = Offset<OffsetDomain::kZlibStream, OffsetUnit::kBytes>;
using ZlibBitOffset = Offset<OffsetDomain::kZlibStream, OffsetUnit::kBits>;
using DeflateBitOffset =
    Offset<OffsetDomain::kDeflatePayload, OffsetUnit::kBits>;
using InflatedByteOffset =
    Offset<OffsetDomain::kInflated, OffsetUnit::kBytes>;

using FileByteRange = Range<OffsetDomain::kFile, OffsetUnit::kBytes>;
using ZlibByteRange = Range<OffsetDomain::kZlibStream, OffsetUnit::kBytes>;
using ZlibBitRange = Range<OffsetDomain::kZlibStream, OffsetUnit::kBits>;
using DeflateBitRange =
    Range<OffsetDomain::kDeflatePayload, OffsetUnit::kBits>;
using InflatedByteRange = Range<OffsetDomain::kInflated, OffsetUnit::kBytes>;

template <OffsetDomain Domain, OffsetUnit Unit>
std::optional<Range<Domain, Unit>> make_range(
    Offset<Domain, Unit> begin, std::uint64_t length) noexcept {
  if (length > std::numeric_limits<std::uint64_t>::max() - begin.value) {
    return std::nullopt;
  }
  return Range<Domain, Unit>{begin,
                             Offset<Domain, Unit>{begin.value + length}};
}

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_OFFSET_RANGE_H
