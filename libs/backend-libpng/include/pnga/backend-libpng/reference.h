#ifndef PNGA_BACKEND_LIBPNG_REFERENCE_H
#define PNGA_BACKEND_LIBPNG_REFERENCE_H

// WP-202: libpng Reference Backend (ADR-0002, ADR-0008). Decodes a PNG through
// public libpng APIs and delivers a stable RGBA8 reference image plus source
// metadata and captured warnings. All error/longjmp behavior is contained
// inside this module; callers never see libpng types.

#include "pnga/io/byte_source.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::backend_libpng {

// Delivered reference image: width x height RGBA8, row-major. `rgba` has
// height * width * 4 bytes. Source color type/bit depth are the ORIGINAL file
// values (transforms are applied for delivery, not to the metadata).
struct ReferenceImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t source_bit_depth = 0;   // original file bit depth
  std::uint8_t source_color_type = 0;  // original PNG color type
  bool interlaced = false;
  std::vector<std::byte> rgba;  // height * width * 4, RGBA8 row-major

  bool empty() const noexcept { return rgba.empty(); }
};

struct ReferenceResult {
  bool success = false;
  std::string error;  // stable message on failure
  ReferenceImage image;
  std::vector<std::string> warnings;  // structured libpng warnings
};

// Decodes `source` as a PNG using public libpng read APIs only (custom read
// callback, info/row/end flow, documented transforms, user/chunk limits).
// Never throws; a malformed or hostile file yields success=false with a stable
// error and no partial image.
ReferenceResult decode_reference(const pnga::io::IByteSource& source);

// The linked libpng version string, e.g. "1.6.58" (for differential evidence).
const char* libpng_version();

}  // namespace pnga::backend_libpng

#endif  // PNGA_BACKEND_LIBPNG_REFERENCE_H
