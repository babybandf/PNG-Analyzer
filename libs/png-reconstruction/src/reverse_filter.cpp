// WP-302 reverse filter implementation. Byte arithmetic wraps modulo 256 by
// using uint8_t; neighbors outside the row or first row are zero.

#include "pnga/png-reconstruction/reverse_filter.h"

#include <cstdlib>
#include <vector>

namespace pnga::png_reconstruction {

bool is_valid_filter_type(std::uint8_t value) noexcept {
  return value <= 4;
}

const char* filter_type_text(FilterType type) noexcept {
  switch (type) {
    case FilterType::kNone:
      return "none";
    case FilterType::kSub:
      return "sub";
    case FilterType::kUp:
      return "up";
    case FilterType::kAverage:
      return "average";
    case FilterType::kPaeth:
      return "paeth";
  }
  return "unknown";
}

std::uint8_t paeth_predictor(std::uint8_t a, std::uint8_t b,
                             std::uint8_t c) noexcept {
  const int p = static_cast<int>(a) + static_cast<int>(b) -
                static_cast<int>(c);
  const int pa = std::abs(p - static_cast<int>(a));
  const int pb = std::abs(p - static_cast<int>(b));
  const int pc = std::abs(p - static_cast<int>(c));
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

std::uint8_t predictor_value(FilterType type, std::uint8_t a, std::uint8_t b,
                             std::uint8_t c) noexcept {
  switch (type) {
    case FilterType::kNone:
      return 0;
    case FilterType::kSub:
      return a;
    case FilterType::kUp:
      return b;
    case FilterType::kAverage:
      return static_cast<std::uint8_t>(
          (static_cast<unsigned>(a) + static_cast<unsigned>(b)) / 2);
    case FilterType::kPaeth:
      return paeth_predictor(a, b, c);
  }
  return 0;
}

std::uint8_t reconstruct_byte(FilterType type, std::uint8_t x, std::uint8_t a,
                              std::uint8_t b, std::uint8_t c) noexcept {
  return static_cast<std::uint8_t>(x + predictor_value(type, a, b, c));
}

namespace {

std::uint8_t to_u8(std::byte b) noexcept {
  return static_cast<std::uint8_t>(b);
}

bool unfilter_impl(FilterType type, std::byte* data, std::size_t row_bytes,
                   const std::byte* prev, std::size_t prev_bytes,
                   std::uint64_t bpp,
                   std::vector<FilterTraceEvent>* events) noexcept {
  if (!is_valid_filter_type(static_cast<std::uint8_t>(type))) {
    return false;
  }
  for (std::size_t i = 0; i < row_bytes; ++i) {
    const std::uint8_t a = i >= bpp ? to_u8(data[i - bpp]) : 0;
    const std::uint8_t b = (prev != nullptr && i < prev_bytes) ? to_u8(prev[i]) : 0;
    const std::uint8_t c = (i >= bpp && prev != nullptr && i < prev_bytes)
                               ? to_u8(prev[i - bpp])
                               : 0;
    const std::uint8_t raw = to_u8(data[i]);
    const std::uint8_t pred = predictor_value(type, a, b, c);
    const std::uint8_t recon = static_cast<std::uint8_t>(raw + pred);
    data[i] = static_cast<std::byte>(recon);
    if (events != nullptr) {
      events->push_back(
          FilterTraceEvent{i, type, raw, a, b, c, pred, recon});
    }
  }
  return true;
}

}  // namespace

bool unfilter_scanline(FilterType type, std::byte* data, std::size_t row_bytes,
                       const std::byte* prev, std::size_t prev_bytes,
                       std::uint64_t bpp) noexcept {
  return unfilter_impl(type, data, row_bytes, prev, prev_bytes, bpp, nullptr);
}

bool unfilter_scanline_traced(FilterType type, std::byte* data,
                              std::size_t row_bytes, const std::byte* prev,
                              std::size_t prev_bytes, std::uint64_t bpp,
                              std::vector<FilterTraceEvent>& events) noexcept {
  return unfilter_impl(type, data, row_bytes, prev, prev_bytes, bpp, &events);
}

}  // namespace pnga::png_reconstruction
