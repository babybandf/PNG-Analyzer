#ifndef PNGA_PNG_RECONSTRUCTION_PASS_RECONSTRUCTION_H
#define PNGA_PNG_RECONSTRUCTION_PASS_RECONSTRUCTION_H

// WP-303: Adam7 pass reconstruction (spec §8.7). Takes the flat filtered
// scanline bytes produced upstream (pass-major stream order), unfilters every
// row with the WP-302 reverse filters, and places the reconstructed samples
// back at their target coordinates. For non-interlaced images this degenerates
// to a plain concat of the unfiltered rows.
//
// The output is the file's own packed storage format: `target` holds
// height * row_bytes(width) bytes, exactly like the reconstructed bytes of a
// non-interlaced image. Unpacking 1/2/4-bit samples into canonical native
// values, palette expansion and color transforms belong to WP-304.

#include "pnga/png-reconstruction/scanline_layout.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pnga::png_reconstruction {

// One pass's unfiltered reduced image in packed storage format. `rows` is
// empty for a zero-size (empty) pass.
struct ReconstructedPass {
  std::uint64_t pass_index = 0;
  std::vector<std::byte> rows;  // passes[p].height * passes[p].row_bytes

  bool operator==(const ReconstructedPass&) const = default;
};

struct PassReconstructionOutcome {
  bool success = false;
  std::string error;  // stable message on failure
  bool interlace = false;
  // One entry per pass (1 for non-interlaced, 7 for Adam7). Distinguishes the
  // per-pass "row artifact" from the fully synthesized `target`.
  std::vector<ReconstructedPass> passes;
  // Full image in packed storage bytes: height * row_bytes(width), Adam7
  // placement fully resolved. Unset on failure.
  std::vector<std::byte> target;
};

// Unfilters every filtered scanline in `filtered` (flat buffer, pass-major
// stream order: pass 0 rows, then pass 1 rows, ...; each row is 1 filter byte
// + `row_bytes` data bytes) and places the reconstructed samples into
// `target`. `layout` must be the layout computed from `header`. Fails with a
// stable error on size mismatch, invalid header/dimensions, an unknown filter
// byte, or any overflowing offset; on failure no partial `target`/`passes`
// result is produced.
PassReconstructionOutcome reconstruct_image(
    const ImageHeader& header, const ScanlineLayout& layout,
    std::span<const std::byte> filtered);

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_PASS_RECONSTRUCTION_H
