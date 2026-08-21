// WP-306 stage analysis implementation. Wires the reconstruction pipeline
// (layout -> inflate_filtered -> reconstruct_image -> native samples) into one
// immutable StageSet and replays a single scanline's filter formula on demand.
// No Qt, no file I/O beyond the borrowed ByteSource.

#include "pnga/analysis-engine/stage_analysis.h"

#include <pnga/png-format/chunk_index.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pnga::analysis_engine {

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

// Reads the 13-byte IHDR body and decodes width/height/bit_depth/color_type
// and the interlace flag (spec §5.2). Chunk bodies are not interpreted by the
// production parser, so this lives here.
std::optional<pnga::png_reconstruction::ImageHeader> parse_ihdr(
    const pnga::png_format::ChunkIndex& index,
    const pnga::io::IByteSource& source) {
  for (const auto& node : index.chunks) {
    if (node.type == std::array<std::byte, 4>{std::byte{'I'}, std::byte{'H'},
                                              std::byte{'D'}, std::byte{'R'}} &&
        node.data_length >= 13) {
      std::array<std::byte, 13> body{};
      if (!source.read(node.data_offset, body.data(), body.size())) {
        return std::nullopt;
      }
      auto u32 = [&](int off) {
        return (static_cast<std::uint32_t>(u8(body[off])) << 24) |
               (static_cast<std::uint32_t>(u8(body[off + 1])) << 16) |
               (static_cast<std::uint32_t>(u8(body[off + 2])) << 8) |
               static_cast<std::uint32_t>(u8(body[off + 3]));
      };
      pnga::png_reconstruction::ImageHeader h;
      h.width = u32(0);
      h.height = u32(4);
      h.bit_depth = u8(body[8]);
      h.color_type = u8(body[9]);
      h.interlace = u8(body[12]) != 0;
      return h;
    }
  }
  return std::nullopt;
}

}  // namespace

StageSet analyze_source(const pnga::io::IByteSource& source) {
  StageSet out;
  const pnga::png_format::ChunkIndex index =
      pnga::png_format::index_chunks(source);
  if (!index.valid_signature) {
    out.error = "invalid PNG signature";
    return out;
  }
  const auto header = parse_ihdr(index, source);
  if (!header.has_value()) {
    out.error = "missing or invalid IHDR";
    return out;
  }
  out.header = *header;
  const pnga::png_format::VirtualIDATStream stream(index);
  StageSet stages = analyze_stages(stream, source, *header);
  return stages;
}

StageSet analyze_stages(const pnga::png_format::VirtualIDATStream& stream,
                        const pnga::io::IByteSource& source,
                        const pnga::png_reconstruction::ImageHeader& header) {
  StageSet out;
  out.header = header;
  out.interlace = header.interlace;

  const auto layout = pnga::png_reconstruction::compute_scanline_layout(header);
  if (!layout.has_value()) {
    out.error = "invalid image header";
    return out;
  }
  const FilteredOutcome filtered = inflate_filtered(stream, source, *layout);
  if (!filtered.success) {
    out.error = filtered.error;
    return out;
  }
  const auto recon =
      pnga::png_reconstruction::reconstruct_image(header, *layout, filtered.filtered);
  if (!recon.success) {
    out.error = recon.error;
    return out;
  }
  const auto native =
      pnga::png_reconstruction::extract_native_samples(header, recon.target);
  if (!native.success) {
    out.error = native.error;
    return out;
  }

  out.scanlines = filtered.scanlines;
  out.passes = recon.passes;
  out.filtered = std::move(filtered.filtered);
  out.unfiltered = std::move(recon.target);
  out.native = std::move(native.image);
  out.success = true;
  return out;
}

FilterFormula filter_formula(const StageSet& set, std::uint64_t row) {
  FilterFormula out;
  out.row = row;
  if (!set.success) {
    out.error = "no stage data";
    return out;
  }
  if (row >= set.scanlines.size()) {
    out.error = "row out of range";
    return out;
  }
  const auto bpp =
      pnga::png_reconstruction::filter_bpp(set.header.bit_depth, set.header.color_type);
  if (!bpp.has_value()) {
    out.error = "invalid bit depth or color type";
    return out;
  }
  const auto layout = pnga::png_reconstruction::compute_scanline_layout(set.header);
  if (!layout.has_value()) {
    out.error = "invalid image header";
    return out;
  }

  // Map stream row -> (pass, row-in-pass). Rows within a pass are consecutive
  // in stream order, so the previous row of the same pass is row-1 when the
  // row is not the first of its pass.
  std::uint64_t cursor = 0;
  std::size_t pass_index = 0;
  std::uint64_t row_in_pass = 0;
  bool found = false;
  for (std::size_t p = 0; p < layout->pass_count; ++p) {
    const auto& pass = layout->passes[p];
    if (pass.height == 0) {
      continue;
    }
    if (row < cursor + pass.height) {
      pass_index = p;
      row_in_pass = row - cursor;
      found = true;
      break;
    }
    cursor += pass.height;
  }
  if (!found) {
    out.error = "row not mapped to a pass";
    return out;
  }

  const FilteredScanlineSpan& span = set.scanlines[row];
  if (span.length == 0 || span.offset + span.length > set.filtered.size()) {
    out.error = "scanline span out of range";
    return out;
  }
  const std::uint64_t data_len = span.length - 1;  // leading filter byte
  out.filter = static_cast<pnga::png_reconstruction::FilterType>(
      static_cast<std::uint8_t>(set.filtered[span.offset]));

  std::vector<std::byte> data(static_cast<std::size_t>(data_len));
  std::memcpy(data.data(), set.filtered.data() + span.offset + 1,
              static_cast<std::size_t>(data_len));

  // Previous reconstructed row of the same pass (zero neighbors for the first
  // row). Taken from the pass artifacts so no replay from pass start is needed.
  std::vector<std::byte> prev;
  if (row_in_pass > 0) {
    const auto& pass = layout->passes[pass_index];
    const auto& rows = set.passes[pass_index].rows;
    const std::size_t prev_off =
        static_cast<std::size_t>((row_in_pass - 1) * pass.row_bytes);
    prev.assign(rows.begin() + prev_off,
                rows.begin() + prev_off + static_cast<std::size_t>(pass.row_bytes));
  }

  if (!pnga::png_reconstruction::unfilter_scanline_traced(
          out.filter, data.data(), static_cast<std::size_t>(data_len),
          prev.data(), prev.size(), *bpp, out.events)) {
    out.error = "invalid filter byte";
    out.events.clear();
    return out;
  }
  out.success = true;
  return out;
}

}  // namespace pnga::analysis_engine
