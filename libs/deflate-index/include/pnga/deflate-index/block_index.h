#ifndef PNGA_DEFLATE_INDEX_BLOCK_INDEX_H
#define PNGA_DEFLATE_INDEX_BLOCK_INDEX_H

// WP-401: fast Deflate block index (REPOSITORY_LAYOUT.md §5.6, ADR-0005/0006).
// A single sequential scan of the zlib stream using inflate(Z_BLOCK) records
// every block's type, BFINAL flag and input/output ranges. Input offsets are
// bit positions in the logical stream; the caller maps them to physical IDAT
// spans through the VirtualIDATStream (this module consumes a generic byte
// stream and never assumes IDAT data is contiguous). No literal/match tokens
// are produced here (that is WP-501+).

#include <pnga/io/byte_source.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::deflate_index {

// Deflate block types (RFC 1951 §3.2.3). A reserved type (3) is a corrupt
// stream; zlib rejects it during the scan, so it never reaches the index.
enum class BlockType { kStored = 0, kFixed = 1, kDynamic = 2 };

const char* block_type_text(BlockType type) noexcept;

// One deflate block. `input_bit_begin/end` are bit offsets in the logical
// (zlib) stream; `output_begin/end` are inflated byte offsets. Ranges are
// half-open; adjacent blocks tile the stream without gaps or overlaps.
struct DeflateBlock {
  std::uint64_t index = 0;
  BlockType type = BlockType::kStored;
  bool last = false;             // BFINAL of this block
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;
};

struct BlockIndexResult {
  bool success = false;
  std::string error;  // stable message on failure
  std::vector<DeflateBlock> blocks;  // stream order
  std::uint64_t zlib_header_bits = 0;  // logical bits before block 0 (2 bytes)
  std::uint64_t total_output_bytes = 0;  // inflated bytes, Adler-verified
  bool adler_ok = true;
};

// Scans the zlib stream exposed by `source` once and records every block.
// `max_output_bytes` caps total inflated output (decompression-bomb
// protection); exceeding it fails with a stable error. The input must be a
// zlib-wrapped stream (2-byte header, deflate blocks, 4-byte Adler-32) and is
// read via read() only — `view()` is never used.
BlockIndexResult index_blocks(const pnga::io::IByteSource& source,
                              std::uint64_t max_output_bytes);

// Index of the block containing inflated byte `output_offset`, or std::nullopt
// when the offset is out of range.
std::optional<std::size_t> block_for_output(const BlockIndexResult& index,
                                            std::uint64_t output_offset);

}  // namespace pnga::deflate_index

#endif  // PNGA_DEFLATE_INDEX_BLOCK_INDEX_H
