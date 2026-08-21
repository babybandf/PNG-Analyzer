#ifndef PNGA_PNG_RECONSTRUCTION_NATIVE_SAMPLES_H
#define PNGA_PNG_RECONSTRUCTION_NATIVE_SAMPLES_H

// WP-304: canonical native samples from the WP-303 packed reconstruction
// (REPOSITORY_LAYOUT.md §5.8). Unpacks 1/2/4-bit samples (ignoring row padding
// bits), reads 8/16-bit samples big-endian, and lays every channel out as one
// uint16 per sample. Palette expansion and color transforms are deliberately
// not performed here (they belong to later delivery work).

#include "pnga/png-reconstruction/scanline_layout.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pnga::png_reconstruction {

// Native sample representation: width*height*channels samples, row-major and
// channel-major within a pixel. Each sample is its raw value; 16-bit values
// are the big-endian byte pair interpreted as uint16, ≤8-bit values fit in the
// low byte. For color type 3 the samples are palette indices.
struct NativeImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;
  std::uint8_t color_type = 0;
  std::uint8_t channels = 0;
  std::vector<std::uint16_t> samples;
};

struct NativeSamplesOutcome {
  bool success = false;
  std::string error;  // stable message on failure
  NativeImage image;
};

// Converts the packed reconstructed target from WP-303 (height *
// row_bytes(width) bytes) into native samples. Fails on an invalid header,
// zero dimensions or a packed buffer size that does not match the layout. On
// failure no partial image is produced.
NativeSamplesOutcome extract_native_samples(
    const ImageHeader& header, std::span<const std::byte> packed);

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_NATIVE_SAMPLES_H
