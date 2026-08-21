// WP-501 token decoder implementation. Reads the Deflate stream bit by bit
// (LSB-first, bounds-checked), decodes stored and fixed-huffman blocks and
// emits one event per literal byte and per length-distance match. Dynamic
// blocks are deferred to WP-502. Reconstructed output is for zlib comparison.

#include "pnga/deflate-trace/token_decoder.h"

#include "pnga/deflate-trace/zlib_wrapper.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace pnga::deflate_trace {

namespace {

constexpr std::size_t kMaxTraceInput = 1u << 26;  // 64 MiB

// LSB-first bit reader over a borrowed byte buffer.
class BitReader {
 public:
  BitReader(const std::byte* data, std::uint64_t bit_size)
      : data_(data), bit_size_(bit_size) {}

  bool read_bit(std::uint64_t* out) {
    if (bit_pos_ >= bit_size_) {
      return false;
    }
    *out = (static_cast<unsigned>(data_[bit_pos_ / 8]) >> (bit_pos_ % 8)) & 1u;
    ++bit_pos_;
    return true;
  }

  // Reads `count` (<= 16) bits as a little-endian integer: the bit at stream
  // position p becomes value bit (p - start).
  bool read_bits(unsigned count, std::uint64_t* out) {
    if (count > 16 || bit_pos_ + count > bit_size_) {
      return false;
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned bit =
          (static_cast<unsigned>(data_[bit_pos_ / 8]) >> (bit_pos_ % 8)) & 1u;
      value |= static_cast<std::uint64_t>(bit) << i;
      ++bit_pos_;
    }
    *out = value;
    return true;
  }

  // Stored-block data is byte aligned; reads the next whole byte.
  bool read_byte(std::byte* out) {
    if (bit_pos_ + 8 > bit_size_) {
      return false;
    }
    *out = data_[bit_pos_ / 8];
    bit_pos_ += 8;
    return true;
  }

  void align_to_byte() { bit_pos_ = (bit_pos_ + 7) & ~std::uint64_t{7}; }

  std::uint64_t pos() const noexcept { return bit_pos_; }
  bool exhausted() const noexcept { return bit_pos_ >= bit_size_; }

 private:
  const std::byte* data_;
  std::uint64_t bit_size_;
  std::uint64_t bit_pos_ = 0;
};

// Fixed-huffman canonical table (RFC 1951 §3.2.6).
struct FixedTable {
  std::array<std::uint16_t, 288> symbols{};
  std::array<std::uint16_t, 16> first_code{};
  std::array<std::uint16_t, 16> first_symbol{};
  std::array<std::uint16_t, 16> bl_count{};
};

const FixedTable& fixed_table() {
  static const FixedTable t = [] {
    FixedTable t;
    std::size_t pos = 0;
    for (std::uint16_t s = 256; s <= 279; ++s) t.symbols[pos++] = s;  // 7 bits
    t.first_symbol[7] = 0;
    t.first_symbol[8] = static_cast<std::uint16_t>(pos);
    for (std::uint16_t s = 0; s <= 143; ++s) t.symbols[pos++] = s;    // 8 bits
    for (std::uint16_t s = 280; s <= 287; ++s) t.symbols[pos++] = s;  // 8 bits
    t.first_symbol[9] = static_cast<std::uint16_t>(pos);
    for (std::uint16_t s = 144; s <= 255; ++s) t.symbols[pos++] = s;  // 9 bits
    t.bl_count[7] = 24;
    t.bl_count[8] = 152;
    t.bl_count[9] = 112;
    std::uint16_t next = 0;
    for (unsigned len = 1; len <= 9; ++len) {
      t.first_code[len] = next;
      next = static_cast<std::uint16_t>((next + t.bl_count[len]) << 1);
    }
    return t;
  }();
  return t;
}

// Canonical huffman decode: the wire sequence is consumed one bit at a time
// in the canonical comparison order. Returns false on truncated or invalid
// input.
bool decode_symbol(BitReader& reader, const FixedTable& t, std::uint16_t* symbol) {
  std::uint64_t code = 0;
  for (unsigned len = 1; len <= 15; ++len) {
    std::uint64_t bit = 0;
    if (!reader.read_bit(&bit)) {
      return false;
    }
    code = (code << 1) | bit;
    if (t.bl_count[len] == 0) {
      continue;
    }
    const std::uint64_t index = code - t.first_code[len];
    if (index < t.bl_count[len]) {
      *symbol = t.symbols[t.first_symbol[len] + index];
      return true;
    }
  }
  return false;
}

struct BaseExtra {
  std::uint16_t base;
  std::uint8_t extra;
};

// Length symbols 257..285 (RFC 1951 §3.2.5).
constexpr std::array<BaseExtra, 29> kLengths = {{
    {3, 0},  {4, 0},  {5, 0},  {6, 0},  {7, 0},  {8, 0},  {9, 0},  {10, 0},
    {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2},
    {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4},
    {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0},
}};

// Distance codes 0..29 (RFC 1951 §3.2.5).
constexpr std::array<BaseExtra, 30> kDistances = {{
    {1, 0},    {2, 0},    {3, 0},    {4, 0},    {5, 1},    {7, 1},
    {9, 2},    {13, 2},   {17, 3},   {25, 3},   {33, 4},   {49, 4},
    {65, 5},   {97, 5},   {129, 6},  {193, 6},  {257, 7},  {385, 7},
    {513, 8},  {769, 8},  {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13},
}};

}  // namespace

TokenDecodeResult decode_stored_and_fixed(const pnga::io::IByteSource& source,
                                          std::uint64_t max_output_bytes) {
  TokenDecodeResult out;
  if (source.size() > kMaxTraceInput) {
    out.error = "input too large for the trace decoder";
    return out;
  }

  const ZlibWrapperTrace wrapper = trace_zlib_wrapper(source);
  if (!wrapper.success) {
    out.error = wrapper.error;
    return out;
  }
  if (wrapper.fdict) {
    out.error = "preset dictionaries (FDICT) are not supported";
    return out;
  }

  std::vector<std::byte> data(static_cast<std::size_t>(source.size()));
  if (!source.read(0, data.data(), data.size())) {
    out.error = "failed to read input stream";
    return out;
  }
  const std::uint64_t start_byte = wrapper.deflate_data_begin;
  if (!wrapper.adler_offset || *wrapper.adler_offset < start_byte) {
    out.error = "invalid zlib data range";
    return out;
  }
  out.deflate_data_begin = start_byte;
  BitReader reader(data.data() + start_byte,
                   (*wrapper.adler_offset - start_byte) * 8);
  const FixedTable& table = fixed_table();

  bool done = false;
  while (!done) {
    std::uint64_t bfinal = 0;
    std::uint64_t btype = 0;
    const std::uint64_t block_begin = reader.pos();
    if (!reader.read_bits(1, &bfinal) || !reader.read_bits(2, &btype)) {
      out.error = "truncated block header";
      return out;
    }

    if (btype == 0) {  // stored
      reader.align_to_byte();
      std::uint64_t len = 0;
      std::uint64_t nlen = 0;
      if (!reader.read_bits(16, &len) || !reader.read_bits(16, &nlen)) {
        out.error = "truncated stored block header";
        return out;
      }
      if (len != ((~nlen) & 0xFFFFu)) {
        out.error = "stored block LEN/NLEN mismatch";
        return out;
      }
      for (std::uint64_t i = 0; i < len; ++i) {
        const std::uint64_t begin = reader.pos();
        std::byte b{0};
        if (!reader.read_byte(&b)) {
          out.error = "truncated stored block data";
          return out;
        }
        TokenEvent token;
        token.kind = TokenKind::kLiteral;
        token.input_bit_begin = begin;
        token.input_bit_end = reader.pos();
        token.output_begin = out.output_bytes;
        token.output_end = out.output_bytes + 1;
        token.literal = static_cast<std::uint8_t>(b);
        out.tokens.push_back(std::move(token));
        out.output.push_back(b);
        ++out.output_bytes;
        if (out.output_bytes > max_output_bytes) {
          out.error = "output cap exceeded";
          return out;
        }
      }
      // A stored block has no end-of-block code; emit the boundary event.
      TokenEvent end;
      end.kind = TokenKind::kEndOfBlock;
      end.input_bit_begin = reader.pos();
      end.input_bit_end = reader.pos();
      end.output_begin = out.output_bytes;
      end.output_end = out.output_bytes;
      out.tokens.push_back(std::move(end));
    } else if (btype == 1) {  // fixed huffman
      while (true) {
        const std::uint64_t begin = reader.pos();
        std::uint16_t symbol = 0;
        if (!decode_symbol(reader, table, &symbol)) {
          out.error = reader.exhausted() ? "truncated fixed code"
                                         : "invalid fixed code";
          return out;
        }
        if (symbol < 256) {
          TokenEvent token;
          token.kind = TokenKind::kLiteral;
          token.input_bit_begin = begin;
          token.input_bit_end = reader.pos();
          token.output_begin = out.output_bytes;
          token.output_end = out.output_bytes + 1;
          token.literal = static_cast<std::uint8_t>(symbol);
          out.tokens.push_back(std::move(token));
          out.output.push_back(static_cast<std::byte>(symbol));
          ++out.output_bytes;
          if (out.output_bytes > max_output_bytes) {
            out.error = "output cap exceeded";
            return out;
          }
        } else if (symbol == 256) {
          TokenEvent token;
          token.kind = TokenKind::kEndOfBlock;
          token.input_bit_begin = begin;
          token.input_bit_end = reader.pos();
          token.output_begin = out.output_bytes;
          token.output_end = out.output_bytes;
          out.tokens.push_back(std::move(token));
          break;  // end of this block
        } else if (symbol <= 285) {
          const auto& le = kLengths[symbol - 257];
          std::uint64_t length = le.base;
          if (le.extra != 0) {
            std::uint64_t extra = 0;
            if (!reader.read_bits(le.extra, &extra)) {
              out.error = "truncated length extra bits";
              return out;
            }
            length += extra;
          }
          std::uint64_t wire_dist_code = 0;
          if (!reader.read_bits(5, &wire_dist_code)) {
            out.error = "truncated distance code";
            return out;
          }
          // Distance codes use a fixed five-bit Huffman alphabet. The generic
          // bit reader returns the wire sequence as an LSB-first integer,
          // while the Huffman table is indexed by the canonical code.
          std::uint64_t dist_code = 0;
          for (unsigned i = 0; i < 5; ++i) {
            dist_code |= ((wire_dist_code >> i) & 1u) << (4u - i);
          }
          if (dist_code > 29) {
            out.error = "invalid distance code";
            return out;
          }
          const auto& de = kDistances[dist_code];
          std::uint64_t distance = de.base;
          if (de.extra != 0) {
            std::uint64_t extra = 0;
            if (!reader.read_bits(de.extra, &extra)) {
              out.error = "truncated distance extra bits";
              return out;
            }
            distance += extra;
          }
          if (distance == 0 || distance > out.output_bytes) {
            out.error = "distance beyond available output";
            return out;
          }
          if (length > max_output_bytes - out.output_bytes) {
            out.error = "output cap exceeded";
            return out;
          }
          const std::uint64_t src = out.output_bytes - distance;
          TokenEvent token;
          token.kind = TokenKind::kLengthDistance;
          token.input_bit_begin = begin;
          token.input_bit_end = reader.pos();
          token.output_begin = out.output_bytes;
          token.output_end = out.output_bytes + length;
          token.length = static_cast<std::uint16_t>(length);
          token.distance = static_cast<std::uint16_t>(distance);
          token.match_source_begin = src;
          token.match_source_end =
              src + std::min<std::uint64_t>(length, distance);
          out.tokens.push_back(std::move(token));
          // Overlap-safe byte copy from the already-decoded window.
          for (std::uint64_t i = 0; i < length; ++i) {
            out.output.push_back(out.output[src + i]);
          }
          out.output_bytes += length;
        } else {
          out.error = "invalid literal/length symbol";
          return out;
        }
      }
    } else {
      out.error = "dynamic huffman blocks are not supported yet (WP-502)";
      return out;
    }

    if (bfinal != 0) {
      done = true;
    }
    (void)block_begin;
  }

  out.stream_ended = true;
  out.success = true;
  return out;
}

}  // namespace pnga::deflate_trace
