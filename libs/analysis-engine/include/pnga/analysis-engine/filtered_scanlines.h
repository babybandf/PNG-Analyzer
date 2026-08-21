#ifndef PNGA_ANALYSIS_ENGINE_FILTERED_SCANLINES_H
#define PNGA_ANALYSIS_ENGINE_FILTERED_SCANLINES_H

// WP-301 orchestration: inflate the logical IDAT stream (via the deflate
// runtime) and split the result into filtered-scanline spans using the
// scanline layout. The engine is the glue layer that may combine the lower
// modules (layout §7); none of them know about the others' concepts.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

// A span into the flat filtered buffer (1 filter byte + data bytes).
struct FilteredScanlineSpan {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;

  bool operator==(const FilteredScanlineSpan&) const = default;
};

struct FilteredOutcome {
  bool success = false;
  std::string error;
  std::vector<std::byte> filtered;            // flat inflated (filtered) bytes
  std::vector<FilteredScanlineSpan> scanlines;  // stream order (pass-major)
  bool exact_size = false;  // inflated bytes == expected layout total
  bool adler_ok = true;
};

// Inflates the virtual IDAT stream and splits it into filtered scanlines per
// `layout`. Fails when the stream is truncated, corrupt, or produces a byte
// count that does not match the layout.
FilteredOutcome inflate_filtered(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source,
    const pnga::png_reconstruction::ScanlineLayout& layout);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_FILTERED_SCANLINES_H
