#ifndef PNGA_DEFLATE_TRACE_TOKEN_DECODER_H
#define PNGA_DEFLATE_TRACE_TOKEN_DECODER_H

// WP-501: token-level Deflate decoder for stored and fixed-huffman blocks
// (RFC 1951 §3.2). A readable, self-contained decoder that emits one event per
// literal byte and per length-distance match, with exact input bit ranges and
// output byte ranges. Dynamic-huffman blocks (WP-502) are rejected for now.
// The reconstructed output lets callers compare byte-for-byte with zlib.

#include <pnga/io/byte_source.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::deflate_trace {

enum class TokenKind { kLiteral, kLengthDistance, kEndOfBlock };

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
};

struct TokenDecodeResult {
  bool success = false;
  std::string error;  // stable message on failure
  std::vector<TokenEvent> tokens;
  std::vector<std::byte> output;  // reconstructed (for zlib comparison)
  bool stream_ended = false;
  std::uint64_t output_bytes = 0;
  std::uint64_t deflate_data_begin = 0;  // byte offset after the zlib wrapper
};

// Decodes the Deflate stream of `source` (a full zlib stream) token by token,
// supporting stored and fixed-huffman blocks. `max_output_bytes` caps the
// reconstructed output (decompression-bomb protection). All bit reads are
// bounds-checked; a truncated or invalid stream fails with a stable error.
TokenDecodeResult decode_stored_and_fixed(const pnga::io::IByteSource& source,
                                          std::uint64_t max_output_bytes);

}  // namespace pnga::deflate_trace

#endif  // PNGA_DEFLATE_TRACE_TOKEN_DECODER_H
