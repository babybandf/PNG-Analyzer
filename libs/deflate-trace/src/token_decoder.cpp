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
#include <exception>
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
    if (count > 16 || bit_pos_ > bit_size_ ||
        static_cast<std::uint64_t>(count) > bit_size_ - bit_pos_) {
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
    if (bit_pos_ > bit_size_ || 8 > bit_size_ - bit_pos_) {
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

struct HuffmanTable {
  std::vector<std::uint16_t> symbols;
  std::vector<std::uint16_t> canonical_codes;
  std::array<std::uint32_t, 16> first_code{};
  std::array<std::uint16_t, 16> first_symbol{};
  std::array<std::uint16_t, 16> bl_count{};
  std::uint8_t max_bits = 0;
  bool empty = true;
  bool complete = false;
};

struct CodeLengthProvenance {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
};

// Builds a canonical table from RFC 1951 code lengths. `allow_incomplete`
// covers the legal one-bit degenerate tree used by Deflate; oversubscribed
// trees are never accepted.
bool build_huffman_table(const std::vector<std::uint8_t>& lengths,
                         unsigned max_bits, bool allow_incomplete,
                         HuffmanTable* table, std::string* error) {
  if (max_bits == 0 || max_bits > 15 || lengths.empty()) {
    *error = "invalid huffman table dimensions";
    return false;
  }

  table->symbols.clear();
  table->canonical_codes.assign(lengths.size(), 0);
  table->first_code.fill(0);
  table->first_symbol.fill(0);
  table->bl_count.fill(0);
  table->max_bits = static_cast<std::uint8_t>(max_bits);
  table->empty = true;
  table->complete = false;

  unsigned actual_max_bits = 0;
  for (const std::uint8_t length : lengths) {
    if (length > max_bits) {
      *error = "huffman code length exceeds the table limit";
      return false;
    }
    if (length != 0) {
      table->empty = false;
      actual_max_bits = std::max(actual_max_bits, static_cast<unsigned>(length));
      ++table->bl_count[length];
    }
  }
  if (table->empty) {
    return true;
  }

  std::int32_t left = 1;
  for (unsigned bits = 1; bits <= max_bits; ++bits) {
    left = (left << 1) - table->bl_count[bits];
    if (left < 0) {
      *error = "oversubscribed huffman tree";
      return false;
    }
  }
  table->complete = left == 0;
  if (!table->complete &&
      (!allow_incomplete || actual_max_bits != 1)) {
    *error = "incomplete huffman tree";
    return false;
  }

  std::uint32_t code = 0;
  for (unsigned bits = 1; bits <= max_bits; ++bits) {
    code = (code + table->bl_count[bits - 1]) << 1;
    table->first_code[bits] = code;
  }

  std::uint16_t symbol_cursor = 0;
  for (unsigned bits = 1; bits <= max_bits; ++bits) {
    table->first_symbol[bits] = symbol_cursor;
    symbol_cursor = static_cast<std::uint16_t>(
        symbol_cursor + table->bl_count[bits]);
  }
  table->symbols.resize(symbol_cursor);
  std::array<std::uint16_t, 16> next_symbol = table->first_symbol;
  std::array<std::uint32_t, 16> next_code = table->first_code;
  for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
    const unsigned bits = lengths[symbol];
    if (bits == 0) {
      continue;
    }
    table->canonical_codes[symbol] = static_cast<std::uint16_t>(next_code[bits]);
    table->symbols[next_symbol[bits]++] = static_cast<std::uint16_t>(symbol);
    ++next_code[bits];
  }
  return true;
}

const HuffmanTable& fixed_literal_table() {
  static const HuffmanTable table = [] {
    HuffmanTable result;
    std::vector<std::uint8_t> lengths(288, 0);
    for (std::size_t symbol = 256; symbol <= 279; ++symbol) {
      lengths[symbol] = 7;
    }
    for (std::size_t symbol = 0; symbol <= 143; ++symbol) {
      lengths[symbol] = 8;
    }
    for (std::size_t symbol = 280; symbol <= 287; ++symbol) {
      lengths[symbol] = 8;
    }
    for (std::size_t symbol = 144; symbol <= 255; ++symbol) {
      lengths[symbol] = 9;
    }
    std::string error;
    if (!build_huffman_table(lengths, 15, false, &result, &error)) {
      std::terminate();
    }
    return result;
  }();
  return table;
}

const HuffmanTable& fixed_distance_table() {
  static const HuffmanTable table = [] {
    HuffmanTable result;
    std::vector<std::uint8_t> lengths(32, 5);
    std::string error;
    if (!build_huffman_table(lengths, 15, false, &result, &error)) {
      std::terminate();
    }
    return result;
  }();
  return table;
}

// Canonical huffman decode: the wire sequence is consumed one bit at a time
// in the canonical comparison order. Returns false on truncated or invalid
// input.
bool decode_symbol(BitReader& reader, const HuffmanTable& t,
                   std::uint16_t* symbol) {
  if (t.empty) {
    return false;
  }
  std::uint64_t code = 0;
  for (unsigned len = 1; len <= t.max_bits; ++len) {
    std::uint64_t bit = 0;
    if (!reader.read_bit(&bit)) {
      return false;
    }
    code = (code << 1) | bit;
    if (t.bl_count[len] == 0 || code < t.first_code[len]) {
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

void append_table_trace(
    pnga::deflate_trace::HuffmanTableKind kind,
    const std::vector<std::uint8_t>& lengths,
    const std::vector<CodeLengthProvenance>& provenance,
    const HuffmanTable& table,
    std::vector<pnga::deflate_trace::HuffmanTableTrace>* traces) {
  pnga::deflate_trace::HuffmanTableTrace trace;
  trace.kind = kind;
  trace.entries.reserve(lengths.size());
  for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
    pnga::deflate_trace::HuffmanTableEntry entry;
    entry.symbol = static_cast<std::uint16_t>(symbol);
    entry.bit_length = lengths[symbol];
    entry.canonical_code = table.canonical_codes[symbol];
    if (symbol < provenance.size()) {
      entry.provenance_bit_begin = provenance[symbol].begin;
      entry.provenance_bit_end = provenance[symbol].end;
    }
    trace.entries.push_back(entry);
  }
  traces->push_back(std::move(trace));
}

bool read_dynamic_tables(
    BitReader& reader, TokenDecodeResult* result, HuffmanTable* literal_table,
    HuffmanTable* distance_table, std::string* error) {
  constexpr std::array<unsigned, 19> kCodeLengthOrder = {
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
      11, 4, 12, 3, 13, 2, 14, 1, 15};

  std::uint64_t hlit_bits = 0;
  std::uint64_t hdist_bits = 0;
  std::uint64_t hclen_bits = 0;
  if (!reader.read_bits(5, &hlit_bits) || !reader.read_bits(5, &hdist_bits) ||
      !reader.read_bits(4, &hclen_bits)) {
    *error = "truncated dynamic header";
    return false;
  }
  const std::size_t literal_count = static_cast<std::size_t>(hlit_bits + 257);
  const std::size_t distance_count = static_cast<std::size_t>(hdist_bits + 1);
  const std::size_t code_length_count = static_cast<std::size_t>(hclen_bits + 4);

  std::vector<std::uint8_t> code_length_lengths(19, 0);
  std::vector<CodeLengthProvenance> code_length_provenance(19);
  for (std::size_t i = 0; i < code_length_count; ++i) {
    const std::uint64_t begin = reader.pos();
    std::uint64_t length = 0;
    if (!reader.read_bits(3, &length)) {
      *error = "truncated code-length alphabet";
      return false;
    }
    const std::size_t symbol = kCodeLengthOrder[i];
    code_length_lengths[symbol] = static_cast<std::uint8_t>(length);
    code_length_provenance[symbol] = {begin, reader.pos()};
  }

  HuffmanTable code_length_table;
  if (!build_huffman_table(code_length_lengths, 7, false,
                           &code_length_table, error)) {
    return false;
  }
  append_table_trace(pnga::deflate_trace::HuffmanTableKind::kCodeLength,
                     code_length_lengths, code_length_provenance,
                     code_length_table, &result->huffman_tables);

  const std::size_t total_lengths = literal_count + distance_count;
  std::vector<std::uint8_t> all_lengths;
  std::vector<CodeLengthProvenance> all_provenance;
  all_lengths.reserve(total_lengths);
  all_provenance.reserve(total_lengths);
  while (all_lengths.size() < total_lengths) {
    const std::uint64_t begin = reader.pos();
    std::uint16_t symbol = 0;
    if (!decode_symbol(reader, code_length_table, &symbol)) {
      *error = reader.exhausted() ? "truncated code-length code"
                                  : "invalid code-length code";
      return false;
    }
    if (symbol <= 15) {
      all_lengths.push_back(static_cast<std::uint8_t>(symbol));
      all_provenance.push_back({begin, reader.pos()});
      continue;
    }

    unsigned extra_bits = 0;
    std::size_t repeat_min = 0;
    std::size_t repeat_max = 0;
    std::uint8_t repeated_length = 0;
    if (symbol == 16) {
      if (all_lengths.empty()) {
        *error = "repeat code 16 has no previous length";
        return false;
      }
      extra_bits = 2;
      repeat_min = 3;
      repeat_max = 6;
      repeated_length = all_lengths.back();
    } else if (symbol == 17) {
      extra_bits = 3;
      repeat_min = 3;
      repeat_max = 10;
    } else if (symbol == 18) {
      extra_bits = 7;
      repeat_min = 11;
      repeat_max = 138;
    } else {
      *error = "invalid code-length repeat symbol";
      return false;
    }

    std::uint64_t extra = 0;
    if (!reader.read_bits(extra_bits, &extra)) {
      *error = "truncated code-length repeat";
      return false;
    }
    const std::size_t repeat = repeat_min + static_cast<std::size_t>(extra);
    if (repeat > repeat_max || repeat > total_lengths - all_lengths.size()) {
      *error = "code-length repeat exceeds the dynamic table";
      return false;
    }
    const CodeLengthProvenance provenance{begin, reader.pos()};
    for (std::size_t i = 0; i < repeat; ++i) {
      all_lengths.push_back(repeated_length);
      all_provenance.push_back(provenance);
    }
  }

  std::vector<std::uint8_t> literal_lengths(
      all_lengths.begin(), all_lengths.begin() + literal_count);
  std::vector<std::uint8_t> distance_lengths(
      all_lengths.begin() + literal_count, all_lengths.end());
  std::vector<CodeLengthProvenance> literal_provenance(
      all_provenance.begin(), all_provenance.begin() + literal_count);
  std::vector<CodeLengthProvenance> distance_provenance(
      all_provenance.begin() + literal_count, all_provenance.end());

  if (literal_lengths[256] == 0) {
    *error = "dynamic literal/length table has no end-of-block code";
    return false;
  }
  if (!build_huffman_table(literal_lengths, 15, true, literal_table, error)) {
    return false;
  }
  if (!build_huffman_table(distance_lengths, 15, true, distance_table,
                           error)) {
    return false;
  }
  append_table_trace(pnga::deflate_trace::HuffmanTableKind::kLiteralLength,
                     literal_lengths, literal_provenance, *literal_table,
                     &result->huffman_tables);
  append_table_trace(pnga::deflate_trace::HuffmanTableKind::kDistance,
                     distance_lengths, distance_provenance, *distance_table,
                     &result->huffman_tables);
  return true;
}

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

  bool done = false;
  while (!done) {
    std::uint64_t bfinal = 0;
    std::uint64_t btype = 0;
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
    } else if (btype == 1 || btype == 2) {  // fixed or dynamic huffman
      HuffmanTable dynamic_literal_table;
      HuffmanTable dynamic_distance_table;
      const HuffmanTable* literal_table = &fixed_literal_table();
      const HuffmanTable* distance_table = &fixed_distance_table();
      if (btype == 2) {
        std::string error;
        if (!read_dynamic_tables(reader, &out, &dynamic_literal_table,
                                 &dynamic_distance_table, &error)) {
          out.error = error;
          return out;
        }
        literal_table = &dynamic_literal_table;
        distance_table = &dynamic_distance_table;
      }
      while (true) {
        const std::uint64_t begin = reader.pos();
        std::uint16_t symbol = 0;
        if (!decode_symbol(reader, *literal_table, &symbol)) {
          out.error = reader.exhausted() ? "truncated huffman code"
                                         : "invalid huffman code";
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
          std::uint16_t dist_code = 0;
          if (distance_table->empty) {
            out.error = "distance table is empty";
            return out;
          }
          if (!decode_symbol(reader, *distance_table, &dist_code)) {
            out.error = reader.exhausted() ? "truncated distance code"
                                           : "invalid distance code";
            return out;
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
      out.error = "reserved deflate block type";
      return out;
    }

    if (bfinal != 0) {
      done = true;
    }
  }

  out.stream_ended = true;
  out.success = true;
  return out;
}

}  // namespace pnga::deflate_trace
