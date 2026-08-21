// WP-304 native sample extraction implementation. Reads the packed
// reconstructed target produced by WP-303 and produces one canonical uint16
// sample per channel. All arithmetic is checked; invalid input yields a
// structured failure, never a wrap or an out-of-bounds read.

#include "pnga/png-reconstruction/native_samples.h"

#include "bit_util.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pnga::png_reconstruction {

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

std::optional<std::uint64_t> checked_mul(std::uint64_t a,
                                         std::uint64_t b) noexcept {
  if (a != 0 && b > kMax / a) {
    return std::nullopt;
  }
  return a * b;
}

bool valid_bit_depth(std::uint8_t bit_depth) noexcept {
  return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
         bit_depth == 8 || bit_depth == 16;
}

}  // namespace

NativeSamplesOutcome extract_native_samples(
    const ImageHeader& header, std::span<const std::byte> packed) {
  NativeSamplesOutcome out;
  NativeImage& image = out.image;

  if (header.width == 0 || header.height == 0) {
    out.error = "invalid image dimensions";
    return out;
  }
  const std::uint8_t channels = channels_for_color_type(header.color_type);
  if (channels == 0 || !valid_bit_depth(header.bit_depth)) {
    out.error = "invalid bit depth or color type";
    return out;
  }
  const auto row_bytes_opt =
      row_bytes(header.width, header.bit_depth, header.color_type);
  if (!row_bytes_opt.has_value()) {
    out.error = "invalid bit depth or color type";
    return out;
  }
  const auto packed_total = checked_mul(header.height, *row_bytes_opt);
  if (!packed_total.has_value() ||
      packed.size() != static_cast<std::size_t>(*packed_total)) {
    out.error = "packed buffer size does not match layout";
    return out;
  }
  const auto area = checked_mul(header.width, header.height);
  const auto sample_count = area.has_value()
                                ? checked_mul(*area, channels)
                                : std::nullopt;
  if (!sample_count.has_value()) {
    out.error = "sample count overflow";
    return out;
  }

  image.width = header.width;
  image.height = header.height;
  image.bit_depth = header.bit_depth;
  image.color_type = header.color_type;
  image.channels = channels;
  image.samples.resize(static_cast<std::size_t>(*sample_count));

  const bool sub_byte = header.bit_depth < 8;
  const unsigned bits = header.bit_depth;
  const unsigned bytes_per_sample = header.bit_depth / 8;

  std::size_t index = 0;
  for (std::uint32_t y = 0; y < header.height; ++y) {
    const std::byte* row = packed.data() + static_cast<std::size_t>(y) *
                                               *row_bytes_opt;
    for (std::uint32_t x = 0; x < header.width; ++x) {
      for (unsigned c = 0; c < channels; ++c) {
        const std::uint64_t sample_bit = (x * channels + c) * bits;
        if (sub_byte) {
          // 1/2/4-bit: unpack one sample; row padding bits are never read.
          image.samples[index++] = detail::read_bits(row, sample_bit, bits);
        } else {
          // 8/16-bit: big-endian bytes. read_bits also works here, but the
          // direct byte path is clearer and avoids per-bit branching.
          const std::byte* p = row + sample_bit / 8;
          std::uint16_t value = 0;
          for (unsigned k = 0; k < bytes_per_sample; ++k) {
            value = static_cast<std::uint16_t>(
                (value << 8) | static_cast<std::uint8_t>(p[k]));
          }
          image.samples[index++] = value;
        }
      }
    }
  }

  out.success = true;
  return out;
}

}  // namespace pnga::png_reconstruction
