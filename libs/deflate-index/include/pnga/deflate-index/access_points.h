#ifndef PNGA_DEFLATE_INDEX_ACCESS_POINTS_H
#define PNGA_DEFLATE_INDEX_ACCESS_POINTS_H

// WP-402: portable Deflate access points (REPOSITORY_LAYOUT.md §5.6,
// ADR-0006). Reference the zran approach: save, at chosen Deflate block
// boundaries, enough state to restart inflate near an arbitrary output offset
// instead of re-decoding from the start. State is fully portable — byte
// offsets, a bit count/value and the preceding 32 KiB of output — never a
// z_stream or a zlib private pointer.

#include <pnga/io/byte_source.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pnga::deflate_index {

// Size of the dictionary a restart point carries (RFC 1951 window).
constexpr std::size_t kWindowSize = 32768;

// One portable inflate restart point at a block boundary. To restart: reset
// inflate, set the dictionary (when output_offset > 0), prime `prime_bits`
// bits with `prime_value`, then feed input starting at `input_byte`.
struct AccessPoint {
  std::uint64_t input_byte = 0;    // byte containing the next block's first bit
  std::uint8_t prime_bits = 0;     // unused bits in that byte to re-insert
  std::uint8_t prime_value = 0;    // their value (LSB-aligned; validation too)
  std::uint64_t output_offset = 0; // inflated bytes before the next block
  std::vector<std::byte> dictionary;  // last 32 KiB of output (empty at 0)
};

struct AccessIndexResult {
  bool success = false;
  std::string error;
  std::vector<AccessPoint> points;  // sorted by output_offset; first at 0
  std::uint64_t total_output_bytes = 0;
  std::array<std::byte, 2> zlib_header{};  // source fingerprint
  bool adler_ok = true;
};

// Scans the zlib stream in `source` once, saving an access point at block
// boundaries at least `min_interval` inflated bytes apart (the first point is
// always at offset 0). `max_output_bytes` caps total inflated output.
AccessIndexResult build_access_index(const pnga::io::IByteSource& source,
                                     std::uint64_t max_output_bytes,
                                     std::uint64_t min_interval);

struct ExtractResult {
  bool success = false;
  std::string error;
  std::vector<std::byte> data;
};

// Extracts `length` inflated bytes starting at `output_offset` by restarting
// from the nearest preceding access point. Fails when the source no longer
// matches the indexed fingerprint (the zlib header or a checkpoint byte
// changed), which rejects stale indexes over a modified file.
ExtractResult extract_output(const AccessIndexResult& index,
                             const pnga::io::IByteSource& source,
                             std::uint64_t output_offset,
                             std::uint64_t length);

}  // namespace pnga::deflate_index

#endif  // PNGA_DEFLATE_INDEX_ACCESS_POINTS_H
