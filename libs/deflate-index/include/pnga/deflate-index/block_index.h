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
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pnga::deflate_index {

// Deflate block types (RFC 1951 §3.2.3). A reserved type (3) is a corrupt
// stream; zlib rejects it during the scan, so it never reaches the index.
enum class BlockType { kStored = 0, kFixed = 1, kDynamic = 2 };

// Typed stop reason of a bounded block scan. The enumeration — never an
// error-string match — is the dispatch contract for callers.
enum class BlockScanStop {
  kComplete,       // the whole stream was indexed successfully
  kBudgetExceeded, // an input, output, block-count or retained-bytes limit
                   // stopped the scan; the verified prefix is kept
  kCancelled,      // the cancellation predicate stopped the scan; the
                   // verified prefix is kept
  kInvalidInput,   // the stream failed to decode (same failures as
                   // index_blocks); the verified prefix is kept
};

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

// Structured zlib stream header facts (RFC 1950 §2.2), read with public byte
// reads before any inflate work.
struct ZlibWrapperInfo {
  std::uint8_t cmf = 0;
  std::uint8_t flg = 0;
  std::uint8_t compression_method = 0;
  std::uint8_t window_bits = 0;
  bool preset_dictionary = false;
  bool header_valid = false;
  bool operator==(const ZlibWrapperInfo&) const = default;
};

enum class Adler32Status { kNotComputed = 0, kMatch = 1, kMismatch = 2 };

// Expected (trailer) versus actual (computed) Adler-32. Both values are only
// present when the corresponding side was actually computed.
struct Adler32Info {
  Adler32Status status = Adler32Status::kNotComputed;
  std::optional<std::uint32_t> expected;
  std::optional<std::uint32_t> actual;
  bool operator==(const Adler32Info&) const = default;
};

struct BlockIndexResult {
  bool success = false;
  std::string error;  // stable message on failure
  std::vector<DeflateBlock> blocks;  // stream order
  std::uint64_t zlib_header_bits = 0;  // logical bits before block 0
  std::uint64_t total_output_bytes = 0;  // inflated bytes
  ZlibWrapperInfo wrapper;
  Adler32Info adler;
  // Input bit / output byte where the scan stopped, assigned from the latest
  // verified boundary on every error return; empty on success.
  std::optional<std::uint64_t> stop_input_bit;
  std::optional<std::uint64_t> stop_output_byte;
};

// Scans the zlib stream exposed by `source` once and records every block.
// `max_output_bytes` caps total inflated output (decompression-bomb
// protection); exceeding it fails with a stable error. The input must be a
// zlib-wrapped stream (2-byte header, deflate blocks, 4-byte Adler-32) and is
// read via read() only — `view()` is never used.
BlockIndexResult index_blocks(const pnga::io::IByteSource& source,
                              std::uint64_t max_output_bytes);

// Independent work limits of a bounded block scan. A zero value disables
// that individual limit; callers that need a hard bound must set every
// field they care about. `max_retained_bytes` constrains the real block
// vector capacity including reallocation peaks (the old and the growing
// buffer coexist during a move), not just the stored size.
struct BlockScanLimits {
  std::uint64_t max_input_bytes = 0;    // compressed bytes consumed
  std::uint64_t max_output_bytes = 0;   // inflated bytes produced
  std::uint64_t max_blocks = 0;         // retained block count
  std::uint64_t max_retained_bytes = 0; // retained block capacity, peaks included
};

// Result of a bounded scan: the verified block prefix plus the typed stop
// reason. `index.success` is true only for a complete scan (kComplete);
// every early stop keeps the blocks verified so far and assigns
// index.stop_input_bit / index.stop_output_byte to the latest verified
// boundary (or 0 when no block was verified).
struct BoundedBlockIndexResult {
  BlockIndexResult index;
  BlockScanStop stop = BlockScanStop::kInvalidInput;
};

// Bounded, cancelable variant of index_blocks with the identical decode
// behavior. Checks every limit before the corresponding work happens:
// input reads are trimmed to max_input_bytes, the block vector growth is
// planned against max_retained_bytes (peak = old + new capacity) and
// max_blocks, and `cancelled` is polled at input refills, inside the
// inflate loop and at block boundaries. An early stop never fakes
// success: the prefix stays verified and the stop reason is typed.
BoundedBlockIndexResult index_blocks_bounded(
    const pnga::io::IByteSource& source, const BlockScanLimits& limits,
    const std::function<bool()>& cancelled = {});

// Index of the block containing inflated byte `output_offset`, or std::nullopt
// when the offset is out of range.
std::optional<std::size_t> block_for_output(const BlockIndexResult& index,
                                            std::uint64_t output_offset);

}  // namespace pnga::deflate_index

#endif  // PNGA_DEFLATE_INDEX_BLOCK_INDEX_H
