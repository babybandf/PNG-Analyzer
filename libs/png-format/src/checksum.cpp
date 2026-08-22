// WP-600A: bounded PNG CRC-32 calculation through the approved zlib provider.

#include "pnga/png-format/checksum.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace pnga::png_format {

namespace {

constexpr std::size_t kReadWindow = 64 * 1024;
constexpr std::uint64_t kCrcSize = 4;

std::uint32_t read_u32_be(const std::array<std::byte, kCrcSize>& bytes) noexcept {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}

bool indexed_range_is_readable(const pnga::io::IByteSource& source,
                               std::uint64_t offset,
                               std::uint64_t length) noexcept {
  return offset <= source.size() && length <= source.size() - offset;
}

}  // namespace

std::optional<std::uint32_t> calculate_chunk_crc(
    const pnga::io::IByteSource& source, const ChunkNode& node) noexcept {
  if (!indexed_range_is_readable(source, node.data_offset, node.data_length) ||
      !indexed_range_is_readable(source, node.crc_offset, kCrcSize)) {
    return std::nullopt;
  }

  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(node.type.data()),
              static_cast<uInt>(node.type.size()));

  std::array<std::byte, kReadWindow> window{};
  std::uint64_t offset = node.data_offset;
  std::uint64_t remaining = node.data_length;
  while (remaining != 0) {
    const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(
        remaining, static_cast<std::uint64_t>(window.size())));
    if (!source.read(offset, window.data(), take)) {
      return std::nullopt;
    }
    crc = crc32(crc, reinterpret_cast<const Bytef*>(window.data()),
                static_cast<uInt>(take));
    offset += take;
    remaining -= take;
  }
  return static_cast<std::uint32_t>(crc);
}

std::optional<std::uint32_t> read_chunk_crc(
    const pnga::io::IByteSource& source, const ChunkNode& node) noexcept {
  if (!indexed_range_is_readable(source, node.crc_offset, kCrcSize)) {
    return std::nullopt;
  }
  std::array<std::byte, kCrcSize> bytes{};
  if (!source.read(node.crc_offset, bytes.data(), bytes.size())) {
    return std::nullopt;
  }
  return read_u32_be(bytes);
}

}  // namespace pnga::png_format
