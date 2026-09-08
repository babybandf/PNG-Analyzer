#ifndef PNGA_TESTS_COMMON_APNG_FIXTURE_H
#define PNGA_TESTS_COMMON_APNG_FIXTURE_H

#include <pnga/png-format/chunk_index.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace pnga_test {

inline std::byte apng_byte(unsigned int value) {
  return static_cast<std::byte>(value & 0xffu);
}

inline void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(apng_byte(value >> 8));
  out.push_back(apng_byte(value));
}

inline void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(apng_byte(value >> 24));
  out.push_back(apng_byte(value >> 16));
  out.push_back(apng_byte(value >> 8));
  out.push_back(apng_byte(value));
}

inline std::vector<std::byte> zlib_deflate(std::span<const std::byte> raw) {
  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> compressed(static_cast<std::size_t>(bound));
  if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &bound,
                reinterpret_cast<const Bytef*>(raw.data()), raw.size(),
                Z_DEFAULT_COMPRESSION) != Z_OK) {
    return {};
  }
  compressed.resize(static_cast<std::size_t>(bound));
  return compressed;
}

inline std::vector<std::byte> apng_zlib_payload_canvas(
    std::uint32_t width, std::uint32_t height, std::array<std::byte, 4> rgba) {
  const std::size_t row = static_cast<std::size_t>(width) * 4 + 1;
  std::vector<std::byte> raw;
  raw.reserve(row * static_cast<std::size_t>(height));
  for (std::uint32_t y = 0; y < height; ++y) {
    raw.push_back(std::byte{0});
    for (std::uint32_t x = 0; x < width; ++x) {
      raw.insert(raw.end(), {rgba[0], rgba[1], rgba[2], rgba[3]});
    }
  }
  return zlib_deflate(raw);
}

inline std::vector<std::byte> apng_zlib_payload(std::array<std::byte, 4> rgba = {}) {
  const std::array<std::byte, 5> raw = {
      std::byte{0}, rgba[0], rgba[1], rgba[2], rgba[3]};
  return zlib_deflate(raw);
}

inline void append_apng_chunk(std::vector<std::byte>& png, const char* type,
                              std::span<const std::byte> data) {
  append_u32(png, static_cast<std::uint32_t>(data.size()));
  std::array<unsigned char, 4> type_bytes = {
      static_cast<unsigned char>(type[0]), static_cast<unsigned char>(type[1]),
      static_cast<unsigned char>(type[2]), static_cast<unsigned char>(type[3])};
  for (const auto value : type_bytes) {
    png.push_back(apng_byte(value));
  }
  png.insert(png.end(), data.begin(), data.end());
  uLong crc = crc32(0, Z_NULL, 0);
  crc = crc32(crc, type_bytes.data(), type_bytes.size());
  if (!data.empty()) {
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), data.size());
  }
  append_u32(png, static_cast<std::uint32_t>(crc));
}

inline std::vector<std::byte> make_apng_canvas(
    bool default_is_frame,
    std::span<const pnga::png_format::FrameControl> frames,
    std::span<const std::array<std::byte, 4>> colors, std::uint32_t width,
    std::uint32_t height) {
  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());

  std::vector<std::byte> ihdr;
  append_u32(ihdr, width);
  append_u32(ihdr, height);
  ihdr.insert(ihdr.end(), {std::byte{8}, std::byte{6}, std::byte{0},
                              std::byte{0}, std::byte{0}});
  append_apng_chunk(png, "IHDR", ihdr);

  std::vector<std::byte> actl;
  append_u32(actl, static_cast<std::uint32_t>(frames.size()));
  append_u32(actl, 0);
  append_apng_chunk(png, "acTL", actl);

  const auto default_payload = apng_zlib_payload_canvas(width, height, {});
  std::uint32_t sequence = 0;
  if (!default_is_frame) {
    append_apng_chunk(png, "IDAT", default_payload);
  }

  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto& frame = frames[i];
    const auto frame_payload =
        i < colors.size() ? apng_zlib_payload_canvas(width, height, colors[i])
                          : default_payload;
    std::vector<std::byte> fctl;
    append_u32(fctl, sequence++);
    append_u32(fctl, frame.width);
    append_u32(fctl, frame.height);
    append_u32(fctl, frame.x);
    append_u32(fctl, frame.y);
    append_u16(fctl, frame.delay_num);
    append_u16(fctl, frame.delay_den);
    fctl.push_back(apng_byte(frame.dispose));
    fctl.push_back(apng_byte(frame.blend));
    append_apng_chunk(png, "fcTL", fctl);

    if (default_is_frame && i == 0) {
      append_apng_chunk(png, "IDAT", frame_payload);
    } else {
      std::vector<std::byte> fdat;
      append_u32(fdat, sequence++);
      fdat.insert(fdat.end(), frame_payload.begin(), frame_payload.end());
      append_apng_chunk(png, "fdAT", fdat);
    }
  }
  append_apng_chunk(png, "IEND", {});
  return png;
}

inline std::vector<std::byte> make_apng(
    bool default_is_frame,
    std::span<const pnga::png_format::FrameControl> frames,
    std::span<const std::array<std::byte, 4>> colors = {}) {
  return make_apng_canvas(default_is_frame, frames, colors, 1, 1);
}

// --- WP-706 T8 format matrix support ---------------------------------------
//
// Deterministic generators for every legal color type / bit depth / interlace
// combination so the frame analysis matrix can compare analyze_frame output
// against independently computed expectations. Sample values come from a
// fixed pattern (or a caller-supplied function); the delivery rules under
// test (scale 255/max for sub-byte depths, >>8 for 16-bit, palette lookup,
// tRNS comparison at original depth) are reimplemented in the tests.

struct ApngFormat {
  std::uint8_t color_type = 6;  // 0, 2, 3, 4 or 6
  std::uint8_t bit_depth = 8;   // legal for the color type
  bool interlace = false;       // Adam7
  std::size_t fdAT_split = 1;   // split each frame payload across N fdATs
};

inline std::uint8_t apng_channels(std::uint8_t color_type) {
  switch (color_type) {
    case 0:
    case 3:
      return 1;
    case 2:
      return 3;
    case 4:
      return 2;
    default:
      return 4;
  }
}

// The default per-pixel sample pattern; mirrored in the tests.
inline std::uint16_t apng_pattern_sample(std::uint32_t x, std::uint32_t y,
                                         std::uint8_t channel,
                                         std::uint8_t bit_depth) {
  const std::uint32_t max =
      bit_depth == 16 ? 65535u : (1u << bit_depth) - 1u;
  return static_cast<std::uint16_t>(
      (x * 7u + y * 13u + channel * 977u + 5u) % (max + 1u));
}

inline std::array<std::uint8_t, 3> apng_palette_entry(std::uint16_t index) {
  return {static_cast<std::uint8_t>((index * 37 + 12) % 256),
          static_cast<std::uint8_t>((index * 71 + 90) % 256),
          static_cast<std::uint8_t>((index * 13 + 200) % 256)};
}

inline std::vector<std::byte> encode_apng_frame_payload(
    std::uint32_t width, std::uint32_t height, const ApngFormat& format,
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t,
                                      std::uint8_t)>& sample_at) {
  const std::uint8_t channels = apng_channels(format.color_type);
  std::vector<std::byte> out;
  const auto emit_row = [&](auto&& get_sample, std::uint32_t row_width) {
    out.push_back(std::byte{0});  // filter type None
    if (format.bit_depth == 16) {
      for (std::uint32_t x = 0; x < row_width; ++x) {
        for (std::uint8_t c = 0; c < channels; ++c) {
          const auto v = get_sample(x, c);
          out.push_back(std::byte{static_cast<unsigned char>(v >> 8)});
          out.push_back(std::byte{static_cast<unsigned char>(v & 0xff)});
        }
      }
      return;
    }
    unsigned char current = 0;
    std::uint32_t bit_pos = 0;
    for (std::uint32_t x = 0; x < row_width; ++x) {
      for (std::uint8_t c = 0; c < channels; ++c) {
        const auto v = get_sample(x, c);
        for (int bit = format.bit_depth - 1; bit >= 0; --bit) {
          current = static_cast<unsigned char>(
              (current << 1) | ((v >> bit) & 1u));
          if (++bit_pos == 8) {
            out.push_back(std::byte{current});
            current = 0;
            bit_pos = 0;
          }
        }
      }
    }
    if (bit_pos != 0) {
      current = static_cast<unsigned char>(current << (8 - bit_pos));
      out.push_back(std::byte{current});
    }
  };

  if (!format.interlace) {
    for (std::uint32_t y = 0; y < height; ++y) {
      emit_row(
          [&](std::uint32_t x, std::uint8_t c) { return sample_at(x, y, c); },
          width);
    }
    return zlib_deflate(out);
  }

  static constexpr std::uint32_t kXStart[7] = {0, 4, 0, 2, 0, 1, 0};
  static constexpr std::uint32_t kYStart[7] = {0, 0, 4, 0, 2, 0, 1};
  static constexpr std::uint32_t kXStep[7] = {8, 8, 4, 4, 2, 2, 1};
  static constexpr std::uint32_t kYStep[7] = {8, 8, 8, 4, 4, 2, 2};
  for (int pass = 0; pass < 7; ++pass) {
    if (width <= kXStart[pass] || height <= kYStart[pass]) {
      continue;
    }
    const std::uint32_t pass_width =
        (width - kXStart[pass] + kXStep[pass] - 1) / kXStep[pass];
    const std::uint32_t pass_height =
        (height - kYStart[pass] + kYStep[pass] - 1) / kYStep[pass];
    for (std::uint32_t j = 0; j < pass_height; ++j) {
      emit_row(
          [&](std::uint32_t i, std::uint8_t c) {
            return sample_at(kXStart[pass] + i * kXStep[pass],
                             kYStart[pass] + j * kYStep[pass], c);
          },
          pass_width);
    }
  }
  return zlib_deflate(out);
}

inline std::vector<std::byte> make_apng_format(
    bool default_is_frame,
    std::span<const pnga::png_format::FrameControl> frames,
    const ApngFormat& format,
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t,
                                      std::uint8_t)>& sample_at = {},
    std::span<const std::byte> palette = {},
    std::span<const std::byte> transparency = {},
    std::uint32_t canvas_width = 0, std::uint32_t canvas_height = 0) {
  const auto pattern = [&](std::uint32_t x, std::uint32_t y, std::uint8_t c) {
    return sample_at ? sample_at(x, y, c)
                     : apng_pattern_sample(x, y, c, format.bit_depth);
  };
  const std::uint32_t width = canvas_width != 0 ? canvas_width
                              : !frames.empty() ? frames.front().width
                                                : 1;
  const std::uint32_t height = canvas_height != 0 ? canvas_height
                               : !frames.empty() ? frames.front().height
                                                 : 1;

  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());
  std::vector<std::byte> ihdr;
  append_u32(ihdr, width);
  append_u32(ihdr, height);
  ihdr.insert(ihdr.end(),
              {std::byte{static_cast<unsigned char>(format.bit_depth)},
               std::byte{static_cast<unsigned char>(format.color_type)},
               std::byte{0}, std::byte{0},
               std::byte{static_cast<unsigned char>(format.interlace ? 1u : 0u)}});
  append_apng_chunk(png, "IHDR", ihdr);
  if (!palette.empty()) {
    append_apng_chunk(png, "PLTE", palette);
  }
  if (!transparency.empty()) {
    append_apng_chunk(png, "tRNS", transparency);
  }

  std::vector<std::byte> actl;
  append_u32(actl, static_cast<std::uint32_t>(frames.size()));
  append_u32(actl, 0);
  append_apng_chunk(png, "acTL", actl);

  std::uint32_t sequence = 0;
  const auto frame_payload = [&](std::uint32_t w, std::uint32_t h) {
    return encode_apng_frame_payload(w, h, format, pattern);
  };
  const auto append_fdAT = [&](std::vector<std::byte> payload) {
    std::vector<std::byte> fdat;
    append_u32(fdat, sequence++);
    fdat.insert(fdat.end(), payload.begin(), payload.end());
    append_apng_chunk(png, "fdAT", fdat);
  };

  if (!default_is_frame) {
    append_apng_chunk(png, "IDAT", frame_payload(width, height));
  }

  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto& frame = frames[i];
    const auto payload = frame_payload(frame.width, frame.height);
    std::vector<std::byte> fctl;
    append_u32(fctl, sequence++);
    append_u32(fctl, frame.width);
    append_u32(fctl, frame.height);
    append_u32(fctl, frame.x);
    append_u32(fctl, frame.y);
    append_u16(fctl, frame.delay_num);
    append_u16(fctl, frame.delay_den);
    fctl.push_back(apng_byte(frame.dispose));
    fctl.push_back(apng_byte(frame.blend));
    append_apng_chunk(png, "fcTL", fctl);

    if (default_is_frame && i == 0) {
      append_apng_chunk(png, "IDAT", payload);
      continue;
    }
    if (format.fdAT_split <= 1) {
      append_fdAT(payload);
      continue;
    }
    // Partition the frame payload into consecutive non-empty parts, one
    // fdAT each; the frame stream is the concatenation of the parts.
    const std::size_t split = format.fdAT_split;
    const std::size_t chunk = (payload.size() + split - 1) / split;
    std::size_t pos = 0;
    while (pos < payload.size()) {
      const std::size_t take = std::min(chunk, payload.size() - pos);
      append_fdAT(std::vector<std::byte>(
          payload.begin() + static_cast<long>(pos),
          payload.begin() + static_cast<long>(pos + take)));
      pos += take;
    }
  }
  append_apng_chunk(png, "IEND", {});
  return png;
}


inline std::uint32_t fixture_u32(const std::byte* data) {
  return (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[0]))
          << 24) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[1]))
          << 16) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[2]))
          << 8) |
         static_cast<std::uint32_t>(std::to_integer<unsigned int>(data[3]));
}

inline void set_sequence(std::vector<std::byte>& png, std::size_t chunk_ordinal,
                         std::uint32_t sequence) {
  std::uint64_t pos = pnga::png_format::kPngSignature.size();
  for (std::size_t ordinal = 0; pos + 12 <= png.size(); ++ordinal) {
    const std::uint32_t length = fixture_u32(png.data() + pos);
    const std::uint64_t data_offset = pos + 8;
    const std::uint64_t crc_offset = data_offset + length;
    if (crc_offset + 4 > png.size()) {
      return;
    }
    if (ordinal == chunk_ordinal) {
      if (length < 4) {
        return;
      }
      png[data_offset] = apng_byte(sequence >> 24);
      png[data_offset + 1] = apng_byte(sequence >> 16);
      png[data_offset + 2] = apng_byte(sequence >> 8);
      png[data_offset + 3] = apng_byte(sequence);
      uLong crc = crc32(0, Z_NULL, 0);
      crc = crc32(crc, reinterpret_cast<const Bytef*>(png.data() + pos + 4),
                  4);
      crc = crc32(crc, reinterpret_cast<const Bytef*>(png.data() + data_offset),
                  length);
      png[crc_offset] = apng_byte(crc >> 24);
      png[crc_offset + 1] = apng_byte(crc >> 16);
      png[crc_offset + 2] = apng_byte(crc >> 8);
      png[crc_offset + 3] = apng_byte(crc);
      return;
    }
    pos = crc_offset + 4;
  }
}

// Recomputes the CRC of the chunk at `chunk_ordinal` (0-based, signature
// excluded) after a test patched its body; mirrors set_sequence's scan.
inline void refresh_chunk_crc(std::vector<std::byte>& png,
                              std::size_t chunk_ordinal) {
  std::uint64_t pos = pnga::png_format::kPngSignature.size();
  for (std::size_t ordinal = 0; pos + 12 <= png.size(); ++ordinal) {
    const std::uint32_t length = fixture_u32(png.data() + pos);
    const std::uint64_t data_offset = pos + 8;
    const std::uint64_t crc_offset = data_offset + length;
    if (crc_offset + 4 > png.size()) {
      return;
    }
    if (ordinal == chunk_ordinal) {
      uLong crc = crc32(0, Z_NULL, 0);
      crc = crc32(crc, reinterpret_cast<const Bytef*>(png.data() + pos + 4), 4);
      if (length != 0) {
        crc = crc32(crc, reinterpret_cast<const Bytef*>(png.data() + data_offset),
                    length);
      }
      png[crc_offset] = apng_byte(crc >> 24);
      png[crc_offset + 1] = apng_byte(crc >> 16);
      png[crc_offset + 2] = apng_byte(crc >> 8);
      png[crc_offset + 3] = apng_byte(crc);
      return;
    }
    pos = crc_offset + 4;
  }
}

}  // namespace pnga_test

#endif  // PNGA_TESTS_COMMON_APNG_FIXTURE_H
