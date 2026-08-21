// WP-303 pass reconstruction implementation. Reuses the WP-302 reverse filters
// for per-row unfiltering and the WP-300 layout for pass geometry. All offset
// arithmetic is checked; hostile input yields a structured failure, never a
// wrap or an out-of-bounds write.

#include "pnga/png-reconstruction/pass_reconstruction.h"

#include "pnga/png-reconstruction/reverse_filter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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

// Big-endian bit access within packed PNG rows (MSB-first, spec §2.2). Reads
// `bits` (1, 2 or 4) starting at bit offset `bit_pos`. A single sample never
// straddles a byte boundary for these widths, but the loop is written to stay
// correct if it ever does.
std::uint8_t read_bits(const std::byte* data, std::uint64_t bit_pos,
                       unsigned bits) noexcept {
  std::uint8_t value = 0;
  for (unsigned b = 0; b < bits; ++b) {
    const std::uint64_t pos = bit_pos + b;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const unsigned bit =
        (static_cast<unsigned>(data[pos / 8]) >> shift) & 1u;
    value = static_cast<std::uint8_t>((value << 1) | bit);
  }
  return value;
}

void write_bits(std::byte* data, std::uint64_t bit_pos, unsigned bits,
                std::uint8_t value) noexcept {
  for (unsigned b = 0; b < bits; ++b) {
    const std::uint64_t pos = bit_pos + b;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const unsigned bit = (static_cast<unsigned>(value) >> (bits - 1 - b)) & 1u;
    std::byte& byte = data[pos / 8];
    byte = static_cast<std::byte>(
        (static_cast<unsigned>(byte) & ~(1u << shift)) | (bit << shift));
  }
}

}  // namespace

PassReconstructionOutcome reconstruct_image(
    const ImageHeader& header, const ScanlineLayout& layout,
    std::span<const std::byte> filtered) {
  PassReconstructionOutcome out;
  out.interlace = header.interlace;

  // Every failure clears target/passes so no partial result ever escapes.
  auto fail = [&out](std::string message) {
    out.success = false;
    out.error = std::move(message);
    out.passes.clear();
    out.target.clear();
    return out;
  };

  if (header.width == 0 || header.height == 0) {
    return fail("invalid image dimensions");
  }
  if (layout.interlace != header.interlace) {
    return fail("header interlace flag does not match layout");
  }
  if (!layout.total_bytes.has_value() ||
      filtered.size() != *layout.total_bytes) {
    return fail("filtered buffer size does not match layout total");
  }
  const auto bpp = filter_bpp(header.bit_depth, header.color_type);
  if (!bpp.has_value()) {
    return fail("invalid bit depth or color type");
  }
  const auto target_row_bytes =
      row_bytes(header.width, header.bit_depth, header.color_type);
  if (!target_row_bytes.has_value()) {
    return fail("invalid bit depth or color type");
  }
  const auto target_size = checked_mul(header.height, *target_row_bytes);
  if (!target_size.has_value()) {
    return fail("target buffer size overflow");
  }
  out.target.assign(static_cast<std::size_t>(*target_size), std::byte{0});

  const bool sub_byte = header.bit_depth < 8;
  const unsigned sample_bits = header.bit_depth;

  std::uint64_t cursor = 0;
  for (std::size_t p = 0; p < layout.pass_count; ++p) {
    const PassLayout& pass = layout.passes[p];
    ReconstructedPass rp;
    rp.pass_index = p;

    if (pass.width == 0 || pass.height == 0) {
      out.passes.push_back(std::move(rp));  // empty pass artifact
      continue;
    }

    // cursor is bounded by filtered.size() (each consumed row advances it by
    // exactly filter_row_bytes and the totals match), so this subtraction is
    // safe and cannot underflow.
    if (pass.filter_row_bytes > filtered.size() - cursor) {
      return fail("filtered buffer truncated inside pass");
    }

    const auto unfiltered_bytes = checked_mul(pass.height, pass.row_bytes);
    if (!unfiltered_bytes.has_value()) {
      return fail("pass size overflow");
    }
    rp.rows.reserve(static_cast<std::size_t>(*unfiltered_bytes));

    std::vector<std::byte> prev;  // previous unfiltered row of this pass
    std::vector<std::byte> row(static_cast<std::size_t>(pass.row_bytes));

    for (std::uint64_t r = 0; r < pass.height; ++r) {
      if (pass.filter_row_bytes > filtered.size() - cursor) {
        return fail("filtered buffer truncated inside pass");
      }
      const auto filter_type = static_cast<FilterType>(
          static_cast<std::uint8_t>(filtered[cursor]));
      std::memcpy(row.data(), filtered.data() + cursor + 1,
                  static_cast<std::size_t>(pass.row_bytes));
      if (!unfilter_scanline(filter_type, row.data(),
                             static_cast<std::size_t>(pass.row_bytes),
                             prev.data(), prev.size(), *bpp)) {
        return fail("invalid filter byte");
      }
      rp.rows.insert(rp.rows.end(), row.begin(), row.end());

      // Place the reconstructed row into the target at the pass coordinates.
      const std::uint64_t y = pass.y_start + r * pass.y_step;
      if (y >= header.height) {
        return fail("pass row outside image height");
      }
      if (sub_byte) {
        for (std::uint64_t sx = 0; sx < pass.width; ++sx) {
          const std::uint64_t x = pass.x_start + sx * pass.x_step;
          if (x >= header.width) {
            return fail("pass sample outside image width");
          }
          const std::uint8_t value =
              read_bits(row.data(), sx * sample_bits, sample_bits);
          write_bits(out.target.data() + y * (*target_row_bytes),
                     x * sample_bits, sample_bits, value);
        }
      } else {
        // Pass rows are laid out per pixel (channels * bytes_per_sample
        // contiguous bytes); pass.width counts pixels, not channels.
        const std::uint64_t bytes_per_pixel =
            static_cast<std::uint64_t>(channels_for_color_type(header.color_type)) *
            (header.bit_depth / 8);
        for (std::uint64_t sx = 0; sx < pass.width; ++sx) {
          const std::uint64_t x = pass.x_start + sx * pass.x_step;
          if (x >= header.width) {
            return fail("pass sample outside image width");
          }
          const std::uint64_t src_offset = sx * bytes_per_pixel;
          const std::uint64_t dst_offset =
              y * (*target_row_bytes) + x * bytes_per_pixel;
          std::memcpy(out.target.data() + dst_offset,
                      row.data() + src_offset, bytes_per_pixel);
        }
      }

      prev = row;
      cursor += pass.filter_row_bytes;
    }
    out.passes.push_back(std::move(rp));
  }
  out.success = true;
  return out;
}

}  // namespace pnga::png_reconstruction
