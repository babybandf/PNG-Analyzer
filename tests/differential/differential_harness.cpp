// WP-305 differential harness implementation. The Trace side drives the real
// production pipeline from raw bytes; the libpng side uses the raw no-transform
// oracle. Any planted divergence surfaces as a first native-sample difference
// at a precise (row, x, channel).

#include "differential_harness.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace pnga::differential {

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

// Reads the 13-byte IHDR body and decodes width/height/bit_depth/color_type
// and the interlace flag (spec §5.2). Chunk bodies are not interpreted by the
// production parser, so this is harness-side.
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

TraceNative trace_decode(const std::vector<std::byte>& png_bytes) {
  TraceNative out;
  pnga::io::MemoryByteSource source(png_bytes);
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

  const auto layout = pnga::png_reconstruction::compute_scanline_layout(*header);
  if (!layout.has_value()) {
    out.error = "invalid IHDR geometry";
    return out;
  }
  const pnga::png_format::VirtualIDATStream stream(index);
  const auto filtered =
      pnga::analysis_engine::inflate_filtered(stream, source, *layout);
  if (!filtered.success) {
    out.error = "inflate: " + filtered.error;
    return out;
  }
  const auto recon = pnga::png_reconstruction::reconstruct_image(
      *header, *layout, filtered.filtered);
  if (!recon.success) {
    out.error = "reconstruct: " + recon.error;
    return out;
  }
  const auto native =
      pnga::png_reconstruction::extract_native_samples(*header, recon.target);
  if (!native.success) {
    out.error = "native: " + native.error;
    return out;
  }
  out.target = std::move(recon.target);
  out.native = std::move(native.image);
  out.ok = true;
  return out;
}

LibpngNative libpng_decode(const std::vector<std::byte>& png_bytes) {
  LibpngNative out;
  const pnga_test::RawOracle oracle = pnga_test::raw_decode(png_bytes);
  if (!oracle.ok) {
    out.error = oracle.error;
    return out;
  }
  out.header = pnga::png_reconstruction::ImageHeader{
      oracle.w, oracle.h, oracle.bd, oracle.ct, oracle.interlace};
  out.rows = oracle.rows;
  const auto native = pnga::png_reconstruction::extract_native_samples(
      out.header, oracle.rows);
  if (!native.success) {
    out.error = "native: " + native.error;
    return out;
  }
  out.native = std::move(native.image);
  out.ok = true;
  return out;
}

DifferentialResult compare_pngs(const std::vector<std::byte>& trace_png,
                                const std::vector<std::byte>& libpng_png) {
  DifferentialResult r;
  const TraceNative trace = trace_decode(trace_png);
  if (!trace.ok) {
    r.error = "trace: " + trace.error;
    return r;
  }
  const LibpngNative libpng = libpng_decode(libpng_png);
  if (!libpng.ok) {
    r.error = "libpng: " + libpng.error;
    return r;
  }

  r.dimensions_match =
      trace.header.width == libpng.header.width &&
      trace.header.height == libpng.header.height &&
      trace.header.bit_depth == libpng.header.bit_depth &&
      trace.header.color_type == libpng.header.color_type &&
      trace.header.interlace == libpng.header.interlace;

  r.target_matches = trace.target == libpng.rows;

  r.native_matches = trace.native.samples == libpng.native.samples;
  if (!r.native_matches) {
    const auto& a = trace.native.samples;
    const auto& b = libpng.native.samples;
    const std::size_t shared = std::min(a.size(), b.size());
    std::size_t idx = 0;
    while (idx < shared && a[idx] == b[idx]) {
      ++idx;
    }
    const unsigned channels = trace.native.channels != 0 ? trace.native.channels : 1;
    const std::uint64_t row_stride =
        static_cast<std::uint64_t>(trace.native.width) * channels;
    r.first.found = true;
    r.first.row = static_cast<std::uint32_t>(idx / row_stride);
    r.first.x = static_cast<std::uint32_t>((idx % row_stride) / channels);
    r.first.channel = static_cast<std::uint8_t>(idx % channels);
    if (idx < shared) {
      r.first.trace_value = a[idx];
      r.first.libpng_value = b[idx];
    } else {
      // Sample counts differ; report the tail boundary.
      r.first.trace_value =
          idx < a.size() ? a[idx] : static_cast<std::uint16_t>(0);
      r.first.libpng_value =
          idx < b.size() ? b[idx] : static_cast<std::uint16_t>(0);
    }
  }

  r.ok = r.dimensions_match && r.target_matches && r.native_matches;
  return r;
}

}  // namespace pnga::differential
