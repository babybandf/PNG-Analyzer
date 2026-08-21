// WP-403 scanline anchor implementation. Build: access points + filtered
// scanlines (WP-301), then unfilter rows in stream order, anchoring the first
// row of every Adam7 pass (prev = empty) and further rows every interval_bytes
// of inflated output. Restore: inflate from the nearest deflate access point to
// the anchor's offset, then replay the reverse filters from the anchor's saved
// previous row.

#include "pnga/analysis-engine/scanline_anchor.h"

#include <pnga/deflate-index/access_points.h>
#include <pnga/png-reconstruction/reverse_filter.h>

#include "virtual_idat_source.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace pnga::analysis_engine {

namespace {

// Nearest access-point output offset at or before `output_offset`.
std::uint64_t point_at_or_before(
    const pnga::deflate_index::AccessIndexResult& access,
    std::uint64_t output_offset) {
  if (access.points.empty()) {
    return 0;
  }
  std::size_t lo = 0;
  std::size_t hi = access.points.size();
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (access.points[mid].output_offset <= output_offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return access.points[lo].output_offset;
}

}  // namespace

ScanlineAnchorIndexResult build_scanline_anchors(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source,
    const pnga::png_reconstruction::ImageHeader& header,
    std::uint64_t interval_bytes, std::uint64_t max_output_bytes) {
  ScanlineAnchorIndexResult out;
  out.header = header;
  out.interval_bytes = interval_bytes;

  const auto layout = pnga::png_reconstruction::compute_scanline_layout(header);
  if (!layout.has_value()) {
    out.error = "invalid image header";
    return out;
  }
  out.layout = *layout;
  const std::uint64_t expected = layout->total_bytes.value_or(0);

  VirtualIdatSource adapter(stream, source);

  // 1. Deflate access points for random access into the inflated stream.
  out.access = pnga::deflate_index::build_access_index(
      adapter, max_output_bytes, std::max<std::uint64_t>(1, interval_bytes));
  if (!out.access.success) {
    out.error = "access index: " + out.access.error;
    return out;
  }

  // 2. Filtered scanlines (WP-301) provide the row spans and flat bytes.
  const FilteredOutcome filtered = inflate_filtered(stream, source, *layout);
  if (!filtered.success) {
    out.error = filtered.error;
    return out;
  }
  out.scanlines = filtered.scanlines;
  out.scanline_count = filtered.scanlines.size();

  const auto bpp = pnga::png_reconstruction::filter_bpp(
      header.bit_depth, header.color_type);
  if (!bpp.has_value()) {
    out.error = "invalid bit depth or color type";
    return out;
  }

  // 3. Unfilter rows in stream order, saving an anchor at the first row of each
  // pass (prev = empty) and at rows at least `interval_bytes` apart.
  std::uint64_t last_anchor_offset = 0;
  bool have_anchor = false;
  std::uint64_t row = 0;
  for (std::size_t p = 0; p < layout->pass_count; ++p) {
    const auto& pass = layout->passes[p];
    if (pass.height == 0) {
      continue;
    }
    std::vector<std::byte> prev;  // reconstructed previous row of this pass
    for (std::uint64_t r = 0; r < pass.height; ++r, ++row) {
      const auto& span = filtered.scanlines[row];
      const bool pass_first = (r == 0);
      if (!have_anchor || pass_first ||
          span.offset - last_anchor_offset >= interval_bytes) {
        RowAnchor anchor;
        anchor.stream_row = row;
        anchor.inflated_offset = span.offset;
        anchor.prev_row = prev;
        out.anchors.push_back(std::move(anchor));
        last_anchor_offset = span.offset;
        have_anchor = true;
      }
      const auto filter_type = static_cast<pnga::png_reconstruction::FilterType>(
          static_cast<std::uint8_t>(filtered.filtered[span.offset]));
      std::vector<std::byte> data(filtered.filtered.begin() + span.offset + 1,
                                  filtered.filtered.begin() + span.offset +
                                      span.length);
      if (!pnga::png_reconstruction::unfilter_scanline(
              filter_type, data.data(), data.size(), prev.data(), prev.size(),
              *bpp)) {
        out.error = "invalid filter byte";
        return out;
      }
      prev = std::move(data);
    }
  }

  // 4. Replay distance: inflated bytes between the nearest access point and
  // each anchor (the discard cost of a restore anchored there).
  out.max_replay_bytes = 0;
  for (const auto& anchor : out.anchors) {
    const std::uint64_t point = point_at_or_before(out.access, anchor.inflated_offset);
    out.max_replay_bytes = std::max(out.max_replay_bytes,
                                    anchor.inflated_offset - point);
  }

  out.success = true;
  return out;
}

RowRestoreResult restore_scanline(
    const ScanlineAnchorIndexResult& index,
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source, std::uint64_t stream_row) {
  RowRestoreResult out;
  if (!index.success || index.anchors.empty()) {
    out.error = "no scanline anchor index";
    return out;
  }
  if (stream_row >= index.scanline_count ||
      stream_row >= index.scanlines.size()) {
    out.error = "scanline out of range";
    return out;
  }

  // Nearest anchor at or before the target row (always within its pass, since
  // each pass's first row is anchored).
  std::size_t lo = 0;
  std::size_t hi = index.anchors.size();
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (index.anchors[mid].stream_row <= stream_row) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const RowAnchor& anchor = index.anchors[lo];

  // Filtered bytes of rows [anchor.stream_row, stream_row], re-decoded from the
  // nearest deflate access point (which extract_output finds internally).
  const auto& end_span = index.scanlines[stream_row];
  const std::uint64_t needed =
      (end_span.offset + end_span.length) - anchor.inflated_offset;
  const std::uint64_t point =
      point_at_or_before(index.access, anchor.inflated_offset);
  out.replay_bytes = anchor.inflated_offset - point;

  VirtualIdatSource adapter(stream, source);
  const auto extracted = pnga::deflate_index::extract_output(
      index.access, adapter, anchor.inflated_offset, needed);
  if (!extracted.success) {
    out.error = "extract: " + extracted.error;
    return out;
  }

  const auto bpp = pnga::png_reconstruction::filter_bpp(
      index.header.bit_depth, index.header.color_type);
  if (!bpp.has_value()) {
    out.error = "invalid bit depth or color type";
    return out;
  }

  // Replay the reverse filters from the anchor's saved previous row.
  std::vector<std::byte> prev = anchor.prev_row;
  std::uint64_t data_pos = 0;
  for (std::uint64_t r = anchor.stream_row; r <= stream_row; ++r) {
    const auto& span = index.scanlines[r];
    const auto filter_type = static_cast<pnga::png_reconstruction::FilterType>(
        static_cast<std::uint8_t>(extracted.data[data_pos]));
    std::vector<std::byte> data(
        extracted.data.begin() +
            static_cast<std::ptrdiff_t>(data_pos + 1),
        extracted.data.begin() +
            static_cast<std::ptrdiff_t>(data_pos + span.length));
    if (!pnga::png_reconstruction::unfilter_scanline(
            filter_type, data.data(), data.size(), prev.data(), prev.size(),
            *bpp)) {
      out.error = "invalid filter byte";
      return out;
    }
    prev = std::move(data);
    data_pos += span.length;
  }

  out.unfiltered = std::move(prev);
  out.success = true;
  return out;
}

}  // namespace pnga::analysis_engine
