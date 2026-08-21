#ifndef PNGA_PNG_RECONSTRUCTION_SRC_BIT_UTIL_H
#define PNGA_PNG_RECONSTRUCTION_SRC_BIT_UTIL_H

// Internal big-endian bit access shared by the reconstruction pipeline. Packed
// PNG rows are MSB-first (spec §2.2), so bit 0 of a row is the most
// significant bit of byte 0. Shared between pass placement and native sample
// extraction so the two never disagree on bit order. Not a public header.

#include <cstddef>
#include <cstdint>

namespace pnga::png_reconstruction::detail {

// Reads `bits` (1, 2 or 4) starting at bit offset `bit_pos`. A single sample
// never straddles a byte boundary for these widths, but the loop stays correct
// if it ever does.
inline std::uint8_t read_bits(const std::byte* data, std::uint64_t bit_pos,
                              unsigned bits) noexcept {
  std::uint8_t value = 0;
  for (unsigned b = 0; b < bits; ++b) {
    const std::uint64_t pos = bit_pos + b;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const unsigned bit =
        (static_cast<unsigned>(data[pos / 8]) >> shift) & 1u;
    value = static_cast<std::uint8_t>((value << 1) | bit);
  }
  return value;
}

// Writes the low `bits` of `value` starting at bit offset `bit_pos`.
inline void write_bits(std::byte* data, std::uint64_t bit_pos, unsigned bits,
                       std::uint8_t value) noexcept {
  for (unsigned b = 0; b < bits; ++b) {
    const std::uint64_t pos = bit_pos + b;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const unsigned bit = (static_cast<unsigned>(value) >> (bits - 1 - b)) & 1u;
    std::byte& byte = data[pos / 8];
    byte = static_cast<std::byte>(
        (static_cast<unsigned>(byte) & ~(1u << shift)) | (bit << shift));
  }
}

}  // namespace pnga::png_reconstruction::detail

#endif  // PNGA_PNG_RECONSTRUCTION_SRC_BIT_UTIL_H
