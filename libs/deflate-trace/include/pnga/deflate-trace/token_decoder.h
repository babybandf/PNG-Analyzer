#ifndef PNGA_DEFLATE_TRACE_TOKEN_DECODER_H
#define PNGA_DEFLATE_TRACE_TOKEN_DECODER_H

// WP-501/502/503: token-level Deflate decoder for stored, fixed- and
// dynamic-huffman blocks (RFC 1951 §3.2), with a bounded LZ window and
// output/source interval provenance. A readable, self-contained decoder emits
// one event per literal byte and per length-distance match, with exact input
// bit ranges and output byte ranges. The reconstructed output lets callers
// compare byte-for-byte with zlib.

#include <pnga/io/byte_source.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::deflate_trace {

enum class TokenKind { kLiteral, kLengthDistance, kEndOfBlock };

enum class HuffmanTableKind { kCodeLength, kLiteralLength, kDistance };

// One canonical Huffman table entry. Provenance spans identify the Deflate
// bits that supplied this entry's code length; repeated code lengths may share
// one span because RFC 1951 encodes them with a repeat instruction.
struct HuffmanTableEntry {
  std::uint16_t symbol = 0;
  std::uint8_t bit_length = 0;
  std::uint16_t canonical_code = 0;
  std::uint64_t provenance_bit_begin = 0;
  std::uint64_t provenance_bit_end = 0;
};

struct HuffmanTableTrace {
  HuffmanTableKind kind = HuffmanTableKind::kCodeLength;
  std::vector<HuffmanTableEntry> entries;
};

// An inflated output interval and the token that produced it. Intervals are
// half-open and non-empty in TokenOutputIntervalIndex. A match source uses
// the same type, so callers can follow a copied byte back to an earlier
// token's output without knowing about the decoder's ring-buffer layout.
struct TokenOutputRange {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  std::uint64_t token_index = 0;

  bool operator==(const TokenOutputRange&) const = default;
};

// Sorted interval index for token output coverage. Deep Trace callers use it
// to map an inflated byte/range to the token(s) that produced it. The index
// owns only compact ranges, not another copy of the inflated output.
class TokenOutputIntervalIndex {
 public:
  void add(TokenOutputRange range);

  const std::vector<TokenOutputRange>& ranges() const noexcept {
    return ranges_;
  }

  std::optional<TokenOutputRange> containing(std::uint64_t offset) const;
  std::vector<TokenOutputRange> overlapping(std::uint64_t begin,
                                            std::uint64_t end) const;

 private:
  std::vector<TokenOutputRange> ranges_;
};

// One decoded token. Bit offsets are relative to the start of the Deflate
// data (i.e. just after the zlib wrapper); output ranges are inflated bytes.
struct TokenEvent {
  TokenKind kind = TokenKind::kLiteral;
  std::uint64_t input_bit_begin = 0;
  std::uint64_t input_bit_end = 0;
  std::uint64_t output_begin = 0;
  std::uint64_t output_end = 0;

  std::uint8_t literal = 0;  // kLiteral: the byte value
  std::uint16_t length = 0;  // kLengthDistance: match length
  std::uint16_t distance = 0;
  std::uint64_t match_source_begin = 0;  // copied output range [begin, end)
  std::uint64_t match_source_end = 0;
  // Root output intervals for the bytes copied by this match. Overlap copies
  // are resolved through the 32 KiB window, so every range points to an
  // earlier token even when the immediate source address is in this token's
  // own output range.
  std::vector<TokenOutputRange> match_source_ranges;
};

struct TokenDecodeResult {
  bool success = false;
  std::string error;  // stable message on failure
  std::vector<TokenEvent> tokens;
  std::vector<HuffmanTableTrace> huffman_tables;
  TokenOutputIntervalIndex output_index;
  std::vector<std::byte> output;  // reconstructed (for zlib comparison)
  bool stream_ended = false;
  std::uint64_t output_bytes = 0;
  std::uint64_t deflate_data_begin = 0;  // byte offset after the zlib wrapper
};

// Decodes the Deflate stream of `source` (a full zlib stream) token by token,
// supporting stored, fixed-huffman and dynamic-huffman blocks.
// `max_output_bytes` caps the reconstructed output (decompression-bomb
// protection). All bit reads are bounds-checked; a truncated or invalid
// stream fails with a stable error.
TokenDecodeResult decode_stored_and_fixed(const pnga::io::IByteSource& source,
                                          std::uint64_t max_output_bytes);

}  // namespace pnga::deflate_trace

#endif  // PNGA_DEFLATE_TRACE_TOKEN_DECODER_H
