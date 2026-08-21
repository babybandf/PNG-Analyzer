#ifndef PNGA_ANALYSIS_ENGINE_SCANLINE_ANCHOR_H
#define PNGA_ANALYSIS_ENGINE_SCANLINE_ANCHOR_H

// WP-403: scanline anchor and filter state (REPOSITORY_LAYOUT.md §5.10,
// ADR-0006). Restores the unfiltered state of an arbitrary scanline quickly:
// inflate from the nearest deflate access point to a saved row anchor, then
// replay the reverse filters from the anchor's saved previous row instead of
// decoding from the start. Qt-free.

#include <pnga/deflate-index/access_points.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include "pnga/analysis-engine/filtered_scanlines.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

// One row anchor: the scanline, its inflated byte offset and the previous
// reconstructed row so unfiltering can resume without decoding from row 0.
struct RowAnchor {
  std::uint64_t stream_row = 0;
  std::uint64_t inflated_offset = 0;  // first byte of this row's filter byte
  std::vector<std::byte> prev_row;    // reconstructed row stream_row - 1
};

struct ScanlineAnchorIndexResult {
  bool success = false;
  std::string error;
  pnga::png_reconstruction::ImageHeader header;
  pnga::png_reconstruction::ScanlineLayout layout;
  std::vector<FilteredScanlineSpan> scanlines;  // stream-order row spans
  std::vector<RowAnchor> anchors;  // sorted by stream_row; first row of each
                                   // Adam7 pass is always anchored
  std::uint64_t scanline_count = 0;
  std::uint64_t interval_bytes = 0;   // anchor spacing in inflated bytes
  std::uint64_t max_replay_bytes = 0; // worst bytes re-decoded from a point
  pnga::deflate_index::AccessIndexResult access;  // deflate access points
};

// Builds the anchor index over the virtual IDAT stream. Anchors are placed at
// scanlines whose inflated offset is at least `interval_bytes` past the
// previous anchor (spacing by bytes keeps anchor memory bounded for very wide
// rows). `max_output_bytes` caps total inflated output.
ScanlineAnchorIndexResult build_scanline_anchors(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source,
    const pnga::png_reconstruction::ImageHeader& header,
    std::uint64_t interval_bytes, std::uint64_t max_output_bytes);

// Restores the unfiltered (reconstructed) bytes of scanline `stream_row`
// (stream order: Adam7 pass-major, row-minor). `replay_bytes` reports how many
// inflated bytes had to be re-decoded for this restore.
struct RowRestoreResult {
  bool success = false;
  std::string error;
  std::vector<std::byte> unfiltered;  // row_bytes bytes
  std::uint64_t replay_bytes = 0;
};
RowRestoreResult restore_scanline(
    const ScanlineAnchorIndexResult& index,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source, std::uint64_t stream_row);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_SCANLINE_ANCHOR_H
