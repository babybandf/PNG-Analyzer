// WP-305 differential harness: run the Trace reconstruction pipeline and the
// libpng raw decoder over the same PNG and report the first native-sample
// difference. The Trace side is the real production path (chunk index ->
// VirtualIDATStream -> inflate_filtered -> Adam7 reconstruction -> native
// samples); the libpng side is the raw no-transform oracle. Test-only; not a
// production library.

#ifndef PNGA_TESTS_DIFFERENTIAL_DIFFERENTIAL_HARNESS_H
#define PNGA_TESTS_DIFFERENTIAL_DIFFERENTIAL_HARNESS_H

#include <pnga/analysis-engine/filtered_scanlines.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/native_samples.h>
#include <pnga/png-reconstruction/pass_reconstruction.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <cstdint>
#include <string>
#include <vector>

#include "test_png_helpers.h"

namespace pnga::differential {

// Location and values of the first native-sample difference between the Trace
// and libpng sides.
struct FirstDifference {
  bool found = false;
  std::uint32_t row = 0;
  std::uint32_t x = 0;
  std::uint8_t channel = 0;
  std::uint16_t trace_value = 0;   // Trace native sample value
  std::uint16_t libpng_value = 0;  // libpng native sample value
};

struct DifferentialResult {
  bool ok = false;  // both sides parsed and every compared field matched
  std::string error;
  bool dimensions_match = false;
  bool target_matches = false;  // Trace packed target == libpng raw rows
  bool native_matches = false;  // native samples identical
  FirstDifference first;        // first native-sample difference (if any)
};

// Trace side: full reconstruction pipeline for one PNG byte buffer.
struct TraceNative {
  bool ok = false;
  std::string error;
  pnga::png_reconstruction::ImageHeader header;
  std::vector<std::byte> target;  // packed reconstruction
  pnga::png_reconstruction::NativeImage native;
};

// Parses the IHDR chunk (13 bytes) into an ImageHeader. Test-side; the
// production parser intentionally does not interpret chunk bodies.
TraceNative trace_decode(const std::vector<std::byte>& png_bytes);

// libpng side: raw no-transform rows converted to native samples.
struct LibpngNative {
  bool ok = false;
  std::string error;
  pnga::png_reconstruction::ImageHeader header;
  std::vector<std::byte> rows;  // raw packed rows
  pnga::png_reconstruction::NativeImage native;
};

LibpngNative libpng_decode(const std::vector<std::byte>& png_bytes);

// Compares the Trace pipeline over `trace_png` with the libpng oracle over
// `libpng_png` (normally the same bytes; different buffers support
// fault-injection tests).
DifferentialResult compare_pngs(const std::vector<std::byte>& trace_png,
                                const std::vector<std::byte>& libpng_png);

// Convenience: both sides over the same file.
inline DifferentialResult compare_png(const std::vector<std::byte>& png_bytes) {
  return compare_pngs(png_bytes, png_bytes);
}

}  // namespace pnga::differential

#endif  // PNGA_TESTS_DIFFERENTIAL_DIFFERENTIAL_HARNESS_H
