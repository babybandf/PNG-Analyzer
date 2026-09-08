#ifndef PNGA_TESTS_COMMON_APNG_FIXTURE_H
#define PNGA_TESTS_COMMON_APNG_FIXTURE_H

#include <pnga/png-format/chunk_index.h>

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

inline std::vector<std::byte> apng_zlib_payload() {
  const std::array<std::byte, 5> raw = {
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
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

inline std::vector<std::byte> make_apng(
    bool default_is_frame,
    std::span<const pnga::png_format::FrameControl> frames) {
  std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                             pnga::png_format::kPngSignature.end());

  std::vector<std::byte> ihdr;
  append_u32(ihdr, 1);
  append_u32(ihdr, 1);
  ihdr.insert(ihdr.end(), {std::byte{8}, std::byte{6}, std::byte{0},
                             std::byte{0}, std::byte{0}});
  append_apng_chunk(png, "IHDR", ihdr);

  std::vector<std::byte> actl;
  append_u32(actl, static_cast<std::uint32_t>(frames.size()));
  append_u32(actl, 0);
  append_apng_chunk(png, "acTL", actl);

  const auto compressed = apng_zlib_payload();
  std::uint32_t sequence = 0;
  if (!default_is_frame) {
    append_apng_chunk(png, "IDAT", compressed);
  }

  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto& frame = frames[i];
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
      append_apng_chunk(png, "IDAT", compressed);
    } else {
      std::vector<std::byte> fdat;
      append_u32(fdat, sequence++);
      fdat.insert(fdat.end(), compressed.begin(), compressed.end());
      append_apng_chunk(png, "fdAT", fdat);
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

}  // namespace pnga_test

#endif  // PNGA_TESTS_COMMON_APNG_FIXTURE_H
