#ifndef PNGA_PNG_RECONSTRUCTION_REVERSE_FILTER_H
#define PNGA_PNG_RECONSTRUCTION_REVERSE_FILTER_H

// WP-302: the five PNG reverse filters (spec §9.4). Filters operate on bytes,
// with bpp = max(1, ceil(bits_per_pixel / 8)); the first row and the first
// bpp bytes use zero neighbors. A scanline carries one leading filter byte,
// then `row_bytes` data bytes. All arithmetic is modulo 256.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pnga::png_reconstruction {

enum class FilterType : std::uint8_t {
  kNone = 0,
  kSub = 1,
  kUp = 2,
  kAverage = 3,
  kPaeth = 4,
};

// Returns true for a valid PNG filter byte value.
bool is_valid_filter_type(std::uint8_t value) noexcept;
const char* filter_type_text(FilterType type) noexcept;

// The Paeth predictor with the spec tie rule: a is preferred, then b.
std::uint8_t paeth_predictor(std::uint8_t a, std::uint8_t b,
                             std::uint8_t c) noexcept;

// Reconstructs one byte from its filtered value and its three neighbors:
//   None     : x
//   Sub      : x + a
//   Up       : x + b
//   Average  : x + (a + b) / 2
//   Paeth    : x + paeth(a, b, c)
// All sums are modulo 256.
std::uint8_t reconstruct_byte(FilterType type, std::uint8_t x, std::uint8_t a,
                              std::uint8_t b, std::uint8_t c) noexcept;

// The predicted neighbor for `type` given (a, b, c) (used by trace events).
std::uint8_t predictor_value(FilterType type, std::uint8_t a, std::uint8_t b,
                             std::uint8_t c) noexcept;

// Unfilters one scanline's data portion in place. `data` holds `row_bytes`
// bytes; `prev` is the previous unfiltered row (may be nullptr for the first
// row). `bpp` is the filter bytes-per-pixel. Returns false for an invalid
// filter type (data is left untouched).
bool unfilter_scanline(FilterType type, std::byte* data, std::size_t row_bytes,
                       const std::byte* prev, std::size_t prev_bytes,
                       std::uint64_t bpp) noexcept;

// Per-byte trace record for the inspector (optional; the fast path above does
// not allocate).
struct FilterTraceEvent {
  std::uint64_t index = 0;
  FilterType type = FilterType::kNone;
  std::uint8_t raw = 0;       // filtered byte as stored
  std::uint8_t a = 0;         // left neighbor
  std::uint8_t b = 0;         // up neighbor
  std::uint8_t c = 0;         // up-left neighbor
  std::uint8_t predictor = 0; // predicted value
  std::uint8_t recon = 0;     // reconstructed value
};

// Like unfilter_scanline, but appends one FilterTraceEvent per byte.
bool unfilter_scanline_traced(FilterType type, std::byte* data,
                              std::size_t row_bytes, const std::byte* prev,
                              std::size_t prev_bytes, std::uint64_t bpp,
                              std::vector<FilterTraceEvent>& events) noexcept;

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_REVERSE_FILTER_H
