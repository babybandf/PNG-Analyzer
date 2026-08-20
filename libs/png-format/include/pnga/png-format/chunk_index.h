#ifndef PNGA_PNG_FORMAT_CHUNK_INDEX_H
#define PNGA_PNG_FORMAT_CHUNK_INDEX_H

// WP-101: PNG signature check and physical Chunk envelope index
// (REPOSITORY_LAYOUT.md §5.3). Scans only Chunk headers; chunk data is never
// parsed or copied. All offset/length arithmetic is checked.

#include "pnga/io/byte_source.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pnga::png_format {

// The 8-byte PNG file signature (PNG spec §5.1).
inline constexpr std::array<std::byte, 8> kPngSignature = {
    std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};

// Physical layout of one Chunk envelope within the source. Offsets are byte
// offsets from the start of the PNG signature (byte 0) and never point into
// copied data: `data_length` bytes live at `data_offset` in the ByteSource.
struct ChunkNode {
  std::uint64_t header_offset = 0;  // first byte of the 8-byte length+type header
  std::uint64_t data_offset = 0;    // first byte of chunk data (= header + 8)
  std::uint64_t data_length = 0;    // declared length; never copied
  std::uint64_t crc_offset = 0;     // first byte of the 4-byte CRC

  // Four ASCII bytes of the Chunk type, e.g. {'I','H','D','R'}.
  std::array<std::byte, 4> type{};

  // Returns the four type bytes as a string ("IHDR", "IDAT", "IEND", ...).
  std::string text() const;
};

// Problems found while scanning. Nodes parsed before the first problem are
// preserved; scanning stops at the first malformed chunk.
enum class ChunkIssueKind {
  kBadSignature,        // at least 8 bytes, but not the PNG signature
  kTruncatedSignature,  // fewer than 8 bytes in the file
  kTruncatedHeader,     // fewer than 8 bytes remain for a length+type header
  kTruncatedData,       // declared length runs past the end of the source
  kTruncatedCrc,        // 4-byte CRC runs past the end of the source
  kTrailingBytesAfterIend,
};

struct ChunkIssue {
  ChunkIssueKind kind;
  std::uint64_t offset;  // byte offset where the problem was detected
};

struct ChunkIndex {
  bool valid_signature = false;
  std::uint64_t file_size = 0;
  std::vector<ChunkNode> chunks;   // envelopes parsed before the first problem
  std::vector<ChunkIssue> issues;  // problems found while scanning
};

// Scans `source` once, validates the PNG signature and builds a physical Chunk
// envelope index (length, type, data span, CRC span) without copying any chunk
// data. Uses checked arithmetic: a malformed or overflowing length keeps the
// nodes parsed so far and reports an issue instead of reading past the source
// or wrapping. Chunk bodies are not interpreted.
ChunkIndex index_chunks(const pnga::io::IByteSource& source);

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_CHUNK_INDEX_H
