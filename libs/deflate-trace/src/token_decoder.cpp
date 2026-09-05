// WP-501/502/503 token decoder implementation. Reads the Deflate stream bit
// by bit (LSB-first, bounds-checked), decodes stored/fixed/dynamic-huffman
// blocks and emits token, output-range and LZ-source provenance events.
// Reconstructed output is for zlib comparison.
// WP-602C: one streaming engine with two sinks — the rich sink builds the
// full TokenDecodeResult, the scalar sink feeds scan_tokens observers with
// aggregate-then-discard TokenFacts over a bounded windowed reader.

#include "pnga/deflate-trace/token_decoder.h"

#include "pnga/deflate-trace/zlib_wrapper.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <vector>
#include <utility>

namespace pnga::deflate_trace {

void TokenOutputIntervalIndex::add(TokenOutputRange range) {
  if (range.begin >= range.end) {
    return;
  }
  if (ranges_.empty() || ranges_.back().begin <= range.begin) {
    ranges_.push_back(range);
    return;
  }
  const auto it = std::lower_bound(
      ranges_.begin(), ranges_.end(), range.begin,
      [](const TokenOutputRange& current, std::uint64_t begin) {
        return current.begin < begin;
      });
  ranges_.insert(it, range);
}

std::optional<TokenOutputRange> TokenOutputIntervalIndex::containing(
    std::uint64_t offset) const {
  const auto it = std::upper_bound(
      ranges_.begin(), ranges_.end(), offset,
      [](std::uint64_t value, const TokenOutputRange& range) {
        return value < range.begin;
      });
  if (it == ranges_.begin()) {
    return std::nullopt;
  }
  const auto& candidate = *std::prev(it);
  if (offset < candidate.end) {
    return candidate;
  }
  return std::nullopt;
}

std::vector<TokenOutputRange> TokenOutputIntervalIndex::overlapping(
    std::uint64_t begin, std::uint64_t end) const {
  std::vector<TokenOutputRange> result;
  if (begin >= end) {
    return result;
  }
  const auto it = std::lower_bound(
      ranges_.begin(), ranges_.end(), begin,
      [](const TokenOutputRange& range, std::uint64_t value) {
        return range.end <= value;
      });
  for (auto current = it; current != ranges_.end() && current->begin < end;
       ++current) {
    result.push_back(*current);
  }
  return result;
}

namespace {

constexpr std::size_t kMaxTraceInput = 1u << 26;  // 64 MiB
constexpr std::uint64_t kLzWindowSize = 32768;

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t* result) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  *result = left + right;
  return true;
}

struct WindowOrigin {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  std::uint64_t token_index = 0;
};

struct WindowEntry {
  std::byte value{0};
  WindowOrigin origin;
};

// A fixed-size ring indexed by absolute inflated output offset. Keeping the
// origin beside each byte is what makes overlap copies traceable: after the
// first distance bytes, a source lookup may hit bytes written by the current
// match, but those bytes already carry the earlier token's root origin.
class LzWindow {
 public:
  LzWindow() : entries_(static_cast<std::size_t>(kLzWindowSize)) {}

  bool read(std::uint64_t output_offset, WindowEntry* entry) const noexcept {
    if (output_offset >= total_output_ ||
        total_output_ - output_offset > kLzWindowSize) {
      return false;
    }
    *entry = entries_[static_cast<std::size_t>(output_offset % kLzWindowSize)];
    return true;
  }

  bool append(std::byte value, WindowOrigin origin) noexcept {
    if (total_output_ == std::numeric_limits<std::uint64_t>::max()) {
      return false;
    }
    entries_[static_cast<std::size_t>(total_output_ % kLzWindowSize)] =
        WindowEntry{value, origin};
    ++total_output_;
    return true;
  }

  std::uint64_t total_output() const noexcept { return total_output_; }

 private:
  // Keep the roughly 1 MiB window off the decoder stack. The capacity is
  // fixed by Deflate, but its storage is still owned by this short-lived
  // deep-trace operation.
  std::vector<WindowEntry> entries_;
  std::uint64_t total_output_ = 0;
};

void append_source_range(std::vector<TokenOutputRange>* ranges,
                         const WindowOrigin& origin) {
  if (origin.begin >= origin.end) {
    return;
  }
  if (!ranges->empty() && ranges->back().token_index == origin.token_index) {
    if (ranges->back().end == origin.begin) {
      ranges->back().end = origin.end;
      return;
    }
    if (ranges->back().begin == origin.begin &&
        ranges->back().end == origin.end) {
      return;
    }
  }
  ranges->push_back(
      TokenOutputRange{origin.begin, origin.end, origin.token_index});
}

// LSB-first bit reader over a bounded sliding window of an IByteSource
// logical range. WP-602C: the reader never maps or copies the whole input —
// it refills a small read window with source.read() only (view() is never
// used) and can check a cancellation hook on every refill. Bit positions are
// absolute offsets within the deflate range, so decoded results are
// identical to the former whole-input reader.
class BitReader {
 public:
  static constexpr std::size_t kWindowBytes = 4096;

  BitReader(const pnga::io::IByteSource& source, std::uint64_t byte_begin,
            std::uint64_t byte_end,
            const std::function<bool()>& should_cancel)
      : source_(source),
        range_begin_(byte_begin),
        range_bytes_(byte_end - byte_begin),
        should_cancel_(should_cancel) {
    buffer_.resize(kWindowBytes);
  }

  bool read_bit(std::uint64_t* out) {
    if (!ensure_bytes(1)) {
      return false;
    }
    *out = (std::to_integer<unsigned>(
                buffer_[static_cast<std::size_t>(bit_pos_ / 8 -
                                                buffer_base_)]) >>
            (bit_pos_ % 8)) & 1u;
    ++bit_pos_;
    return true;
  }

  // Reads `count` (<= 16) bits as a little-endian integer: the bit at stream
  // position p becomes value bit (p - start).
  bool read_bits(unsigned count, std::uint64_t* out) {
    if (count > 16 || bit_pos_ > range_bytes_ * 8 ||
        static_cast<std::uint64_t>(count) > range_bytes_ * 8 - bit_pos_) {
      return false;
    }
    if (!ensure_bytes((bit_pos_ % 8 + count + 7) / 8)) {
      return false;
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned bit =
          (std::to_integer<unsigned>(
               buffer_[static_cast<std::size_t>(bit_pos_ / 8 -
                                                buffer_base_)]) >>
           (bit_pos_ % 8)) & 1u;
      value |= static_cast<std::uint64_t>(bit) << i;
      ++bit_pos_;
    }
    *out = value;
    return true;
  }

  // Stored-block data is byte aligned; reads the next whole byte.
  bool read_byte(std::byte* out) {
    if (bit_pos_ > range_bytes_ * 8 || 8 > range_bytes_ * 8 - bit_pos_) {
      return false;
    }
    if (!ensure_bytes(1)) {
      return false;
    }
    *out = buffer_[static_cast<std::size_t>(bit_pos_ / 8 - buffer_base_)];
    bit_pos_ += 8;
    return true;
  }

  void align_to_byte() { bit_pos_ = (bit_pos_ + 7) & ~std::uint64_t{7}; }

  std::uint64_t pos() const noexcept { return bit_pos_; }
  bool exhausted() const noexcept { return bit_pos_ >= range_bytes_ * 8; }
  bool cancelled() const noexcept { return cancelled_; }
  bool io_failed() const noexcept { return io_failed_; }

 private:
  // Guarantees that the `needed` bytes starting at the current byte position
  // are inside the logical range and resident in the sliding window.
  bool ensure_bytes(std::size_t needed) {
    const std::uint64_t byte_pos = bit_pos_ / 8;
    if (needed > range_bytes_ - byte_pos) {
      return false;
    }
    if (byte_pos + needed <= buffer_base_ + buffer_size_) {
      return true;
    }
    return refill(byte_pos, needed);
  }

  bool refill(std::uint64_t byte_pos, std::size_t needed) {
    // Slide the window: only bytes from the current position onward matter.
    const std::uint64_t keep = byte_pos - buffer_base_;
    if (keep >= buffer_size_) {
      buffer_base_ = byte_pos;
      buffer_size_ = 0;
    } else if (keep != 0) {
      const std::size_t tail = buffer_size_ - static_cast<std::size_t>(keep);
      std::memmove(buffer_.data(), buffer_.data() +
                                       static_cast<std::size_t>(keep), tail);
      buffer_base_ = byte_pos;
      buffer_size_ = tail;
    }
    if (should_cancel_ && should_cancel_()) {
      cancelled_ = true;
      return false;
    }
    const std::uint64_t buffered_end = buffer_base_ + buffer_size_;
    const std::uint64_t remaining = range_bytes_ - buffered_end;
    const std::size_t capacity = buffer_.size() - buffer_size_;
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(capacity, remaining));
    if (want != 0) {
      if (!source_.read(range_begin_ + buffered_end,
                        buffer_.data() + buffer_size_, want)) {
        io_failed_ = true;
        return false;
      }
      buffer_size_ += want;
    }
    return byte_pos + needed <= buffer_base_ + buffer_size_;
  }

  const pnga::io::IByteSource& source_;
  std::uint64_t range_begin_;
  std::uint64_t range_bytes_;
  const std::function<bool()>& should_cancel_;
  std::vector<std::byte> buffer_;
  std::uint64_t buffer_base_ = 0;  // absolute range byte offset of buffer_[0]
  std::size_t buffer_size_ = 0;    // valid bytes in the window
  std::uint64_t bit_pos_ = 0;
  bool cancelled_ = false;
  bool io_failed_ = false;
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

// WP-602C: the dynamic-table reader routes table traces through the sink, so
// the rich decoder records provenance while the scalar scan discards tables.
template <class Sink>
bool read_dynamic_tables(BitReader& reader, Sink& sink,
                         HuffmanTable* literal_table,
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
  sink.on_table(pnga::deflate_trace::HuffmanTableKind::kCodeLength,
                code_length_lengths, code_length_provenance,
                code_length_table);

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
  sink.on_table(pnga::deflate_trace::HuffmanTableKind::kLiteralLength,
                literal_lengths, literal_provenance, *literal_table);
  sink.on_table(pnga::deflate_trace::HuffmanTableKind::kDistance,
                distance_lengths, distance_provenance, *distance_table);
  return true;
}

// --- WP-602C: shared decode engine with rich/scalar sinks -------------------

enum class DecodeFailureKind {
  kNone,         // stream ended
  kMalformed,    // structural zlib/deflate input problem
  kIo,           // source read failure
  kInternal,     // checked-arithmetic or window failure
  kBudget,       // output/token/input budget exceeded
  kCancelled,    // cooperative cancellation
  kObserverStop, // observer asked to stop
};

struct DecodeBudget {
  bool enabled = false;
  std::uint64_t max_input_bytes = 0;  // 0 = unlimited
  std::uint64_t max_tokens = 0;       // 0 = unlimited
};

struct DecodeOutcome {
  bool stream_ended = false;
  std::uint64_t input_bits = 0;
  std::uint64_t output_bytes = 0;
  std::uint64_t token_count = 0;
  DecodeFailureKind failure = DecodeFailureKind::kNone;
  std::string error;
};

// Rich sink: builds the existing TokenDecodeResult — token events with
// provenance, Huffman table traces, the reconstructed output and the 32 KiB
// LZ window needed for overlap source ranges.
class RichSink {
 public:
  TokenDecodeResult result;

  void on_table(HuffmanTableKind kind,
                const std::vector<std::uint8_t>& lengths,
                const std::vector<CodeLengthProvenance>& provenance,
                const HuffmanTable& table) {
    append_table_trace(kind, lengths, provenance, table,
                       &result.huffman_tables);
  }

  bool on_token(TokenKind kind, std::uint64_t bit_begin, std::uint64_t bit_end,
                std::uint64_t output_begin, std::uint64_t output_end,
                std::uint8_t literal, std::uint16_t length,
                std::uint16_t distance, std::uint64_t match_source_begin,
                std::uint64_t match_source_end,
                std::optional<std::uint16_t> huffman_symbol) {
    TokenEvent token;
    token.kind = kind;
    token.input_bit_begin = bit_begin;
    token.input_bit_end = bit_end;
    token.output_begin = output_begin;
    token.output_end = output_end;
    token.literal = literal;
    token.length = length;
    token.distance = distance;
    token.match_source_begin = match_source_begin;
    token.match_source_end = match_source_end;
    token.huffman_symbol = huffman_symbol;
    last_token_index_ = result.tokens.size();
    result.tokens.push_back(std::move(token));
    if (kind != TokenKind::kEndOfBlock) {
      result.output_index.add(
          TokenOutputRange{output_begin, output_end, last_token_index_});
    }
    return true;
  }

  bool emit_literal_byte(std::byte value, std::uint64_t output_begin,
                         std::uint64_t output_end) {
    result.output.push_back(value);
    if (!window_.append(value, WindowOrigin{output_begin, output_end,
                                            last_token_index_})) {
      last_error_ = "LZ window output overflow";
      return false;
    }
    return true;
  }

  bool emit_match_bytes(std::uint64_t output_begin, std::uint16_t length,
                        std::uint16_t distance) {
    // Overlap-safe byte copy from the fixed 32 KiB window. Looking up by the
    // current output cursor, rather than indexing the original output
    // vector, makes the ring-buffer wrap and overlap semantics explicit and
    // keeps source provenance attached to each byte.
    std::uint64_t cursor = output_begin;
    for (std::uint64_t i = 0; i < length; ++i) {
      if (distance > cursor) {
        last_error_ = "distance beyond available window";
        return false;
      }
      const std::uint64_t source_offset = cursor - distance;
      WindowEntry source_entry;
      if (!window_.read(source_offset, &source_entry)) {
        last_error_ = "distance beyond available window";
        return false;
      }
      append_source_range(
          &result.tokens[last_token_index_].match_source_ranges,
          source_entry.origin);
      result.output.push_back(source_entry.value);
      if (!window_.append(source_entry.value, source_entry.origin)) {
        last_error_ = "LZ window output overflow";
        return false;
      }
      ++cursor;
    }
    return true;
  }

  const std::string& last_error() const noexcept { return last_error_; }

 private:
  LzWindow window_;
  std::size_t last_token_index_ = 0;
  std::string last_error_;
};

// Scalar sink: invokes the observer and retains no events, tables or output.
// Only the single in-flight TokenFact exists at any moment.
class ScalarSink {
 public:
  std::uint64_t facts = 0;
  std::uint64_t peak_retained_token_records = 0;
  bool stopped = false;

  explicit ScalarSink(
      const std::function<bool(const TokenFact&)>& observer)
      : observer_(observer) {}

  void on_table(HuffmanTableKind, const std::vector<std::uint8_t>&,
                const std::vector<CodeLengthProvenance>&,
                const HuffmanTable&) {
    // Aggregate-then-discard: Huffman tables are never retained.
  }

  bool on_token(TokenKind kind, std::uint64_t bit_begin, std::uint64_t bit_end,
                std::uint64_t output_begin, std::uint64_t output_end,
                std::uint8_t, std::uint16_t length, std::uint16_t distance,
                std::uint64_t, std::uint64_t, std::optional<std::uint16_t>) {
    TokenFact fact;
    fact.kind = kind;
    fact.input_bits = bit_end - bit_begin;
    fact.output_bytes = output_end - output_begin;
    fact.length = length;
    fact.distance = distance;
    fact.output_begin = output_begin;
    ++facts;
    peak_retained_token_records = 1;
    if (observer_ && !observer_(fact)) {
      stopped = true;
      return false;
    }
    return true;
  }

  bool emit_literal_byte(std::byte, std::uint64_t, std::uint64_t) {
    return true;  // output bytes are counted by the engine, never stored
  }
  bool emit_match_bytes(std::uint64_t, std::uint16_t, std::uint16_t) {
    return true;
  }
  const std::string& last_error() const noexcept { return last_error_; }

 private:
  const std::function<bool(const TokenFact&)>& observer_;
  std::string last_error_;
};

// The one streaming decoder engine behind both entry points. Interprets
// stored/fixed/dynamic blocks once and routes every token fact and output
// byte through the sink; `budget`/`should_cancel` are active only for the
// scalar scan.
template <class Sink>
DecodeOutcome run_deflate_blocks(const pnga::io::IByteSource& source,
                                 std::uint64_t data_begin,
                                 std::uint64_t data_end,
                                 std::uint64_t max_output_bytes,
                                 const std::function<bool()>& should_cancel,
                                 const DecodeBudget& budget, Sink& sink) {
  DecodeOutcome outcome;
  if (data_end < data_begin ||
      data_end - data_begin > std::numeric_limits<std::uint64_t>::max() / 8) {
    outcome.failure = DecodeFailureKind::kInternal;
    outcome.error = "deflate bit range overflow";
    return outcome;
  }
  BitReader reader(source, data_begin, data_end, should_cancel);
  std::uint64_t output_bytes = 0;
  std::uint64_t token_count = 0;

  const auto finish = [&](DecodeFailureKind kind, std::string message) {
    outcome.failure = kind;
    outcome.error = std::move(message);
    outcome.input_bits = reader.pos();
    outcome.output_bytes = output_bytes;
    outcome.token_count = token_count;
    return outcome;
  };
  // Reader failures are truncation unless a refill was cancelled or failed.
  const auto reader_failure = [&](const char* message) {
    if (reader.cancelled()) {
      return finish(DecodeFailureKind::kCancelled, "token scan cancelled");
    }
    if (reader.io_failed()) {
      return finish(DecodeFailureKind::kIo, "failed to read input stream");
    }
    return finish(DecodeFailureKind::kMalformed, message);
  };
  const auto input_over_budget = [&]() {
    if (!budget.enabled || budget.max_input_bytes == 0 ||
        budget.max_input_bytes >
            std::numeric_limits<std::uint64_t>::max() / 8) {
      return false;
    }
    return reader.pos() > budget.max_input_bytes * 8;
  };

  bool done = false;
  while (!done) {
    if (should_cancel && should_cancel()) {
      return finish(DecodeFailureKind::kCancelled, "token scan cancelled");
    }
    if (input_over_budget()) {
      return finish(DecodeFailureKind::kBudget, "input budget exceeded");
    }
    std::uint64_t bfinal = 0;
    std::uint64_t btype = 0;
    if (!reader.read_bits(1, &bfinal) || !reader.read_bits(2, &btype)) {
      return reader_failure("truncated block header");
    }

    if (btype == 0) {  // stored
      reader.align_to_byte();
      std::uint64_t len = 0;
      std::uint64_t nlen = 0;
      if (!reader.read_bits(16, &len) || !reader.read_bits(16, &nlen)) {
        return reader_failure("truncated stored block header");
      }
      if (len != ((~nlen) & 0xFFFFu)) {
        return finish(DecodeFailureKind::kMalformed,
                      "stored block LEN/NLEN mismatch");
      }
      for (std::uint64_t i = 0; i < len; ++i) {
        if (budget.enabled && budget.max_tokens != 0 &&
            token_count >= budget.max_tokens) {
          return finish(DecodeFailureKind::kBudget, "token budget exceeded");
        }
        if (input_over_budget()) {
          return finish(DecodeFailureKind::kBudget, "input budget exceeded");
        }
        if (token_count % 256 == 0 && should_cancel && should_cancel()) {
          return finish(DecodeFailureKind::kCancelled, "token scan cancelled");
        }
        const std::uint64_t begin = reader.pos();
        std::byte b{0};
        if (!reader.read_byte(&b)) {
          return reader_failure("truncated stored block data");
        }
        const std::uint64_t output_begin = output_bytes;
        std::uint64_t output_end = 0;
        if (!checked_add(output_begin, 1, &output_end)) {
          return finish(DecodeFailureKind::kInternal, "output range overflow");
        }
        ++token_count;
        if (!sink.on_token(TokenKind::kLiteral, begin, reader.pos(),
                           output_begin, output_end,
                           std::to_integer<std::uint8_t>(b), 0, 0, 0, 0,
                           std::optional<std::uint16_t>{})) {
          return finish(DecodeFailureKind::kObserverStop, std::string());
        }
        if (!sink.emit_literal_byte(b, output_begin, output_end)) {
          return finish(DecodeFailureKind::kInternal, sink.last_error());
        }
        output_bytes = output_end;
        if (output_bytes > max_output_bytes) {
          return finish(DecodeFailureKind::kBudget, "output cap exceeded");
        }
      }
      // A stored block has no end-of-block code; emit the boundary fact.
      ++token_count;
      if (!sink.on_token(TokenKind::kEndOfBlock, reader.pos(), reader.pos(),
                         output_bytes, output_bytes, 0, 0, 0, 0, 0,
                         std::optional<std::uint16_t>{})) {
        return finish(DecodeFailureKind::kObserverStop, std::string());
      }
    } else if (btype == 1 || btype == 2) {  // fixed or dynamic huffman
      HuffmanTable dynamic_literal_table;
      HuffmanTable dynamic_distance_table;
      const HuffmanTable* literal_table = &fixed_literal_table();
      const HuffmanTable* distance_table = &fixed_distance_table();
      if (btype == 2) {
        std::string error;
        if (!read_dynamic_tables(reader, sink, &dynamic_literal_table,
                                 &dynamic_distance_table, &error)) {
          return finish(DecodeFailureKind::kMalformed, std::move(error));
        }
        literal_table = &dynamic_literal_table;
        distance_table = &dynamic_distance_table;
      }
      while (true) {
        if (budget.enabled && budget.max_tokens != 0 &&
            token_count >= budget.max_tokens) {
          return finish(DecodeFailureKind::kBudget, "token budget exceeded");
        }
        if (input_over_budget()) {
          return finish(DecodeFailureKind::kBudget, "input budget exceeded");
        }
        if (token_count % 256 == 0 && should_cancel && should_cancel()) {
          return finish(DecodeFailureKind::kCancelled, "token scan cancelled");
        }
        const std::uint64_t begin = reader.pos();
        std::uint16_t symbol = 0;
        if (!decode_symbol(reader, *literal_table, &symbol)) {
          return reader_failure(reader.exhausted() ? "truncated huffman code"
                                                   : "invalid huffman code");
        }
        if (symbol < 256) {
          const std::uint64_t output_begin = output_bytes;
          std::uint64_t output_end = 0;
          if (!checked_add(output_begin, 1, &output_end)) {
            return finish(DecodeFailureKind::kInternal,
                          "output range overflow");
          }
          ++token_count;
          if (!sink.on_token(TokenKind::kLiteral, begin, reader.pos(),
                             output_begin, output_end,
                             static_cast<std::uint8_t>(symbol), 0, 0, 0, 0,
                             symbol)) {
            return finish(DecodeFailureKind::kObserverStop, std::string());
          }
          if (!sink.emit_literal_byte(static_cast<std::byte>(symbol),
                                      output_begin, output_end)) {
            return finish(DecodeFailureKind::kInternal, sink.last_error());
          }
          output_bytes = output_end;
          if (output_bytes > max_output_bytes) {
            return finish(DecodeFailureKind::kBudget, "output cap exceeded");
          }
        } else if (symbol == 256) {
          ++token_count;
          if (!sink.on_token(TokenKind::kEndOfBlock, begin, reader.pos(),
                             output_bytes, output_bytes, 0, 0, 0, 0, 0,
                             symbol)) {
            return finish(DecodeFailureKind::kObserverStop, std::string());
          }
          break;  // end of this block
        } else if (symbol <= 285) {
          const auto& le = kLengths[symbol - 257];
          std::uint64_t length = le.base;
          if (le.extra != 0) {
            std::uint64_t extra = 0;
            if (!reader.read_bits(le.extra, &extra)) {
              return reader_failure("truncated length extra bits");
            }
            length += extra;
          }
          if (distance_table->empty) {
            return finish(DecodeFailureKind::kMalformed,
                          "distance table is empty");
          }
          std::uint16_t dist_code = 0;
          if (!decode_symbol(reader, *distance_table, &dist_code)) {
            return reader_failure(reader.exhausted()
                                      ? "truncated distance code"
                                      : "invalid distance code");
          }
          if (dist_code > 29) {
            return finish(DecodeFailureKind::kMalformed,
                          "invalid distance code");
          }
          const auto& de = kDistances[dist_code];
          std::uint64_t distance = de.base;
          if (de.extra != 0) {
            std::uint64_t extra = 0;
            if (!reader.read_bits(de.extra, &extra)) {
              return reader_failure("truncated distance extra bits");
            }
            distance += extra;
          }
          if (distance == 0 || distance > output_bytes) {
            return finish(DecodeFailureKind::kMalformed,
                          "distance beyond available output");
          }
          if (length > max_output_bytes - output_bytes) {
            return finish(DecodeFailureKind::kBudget, "output cap exceeded");
          }
          const std::uint64_t src = output_bytes - distance;
          std::uint64_t source_end = 0;
          if (!checked_add(src, std::min<std::uint64_t>(length, distance),
                           &source_end)) {
            return finish(DecodeFailureKind::kInternal,
                          "match source range overflow");
          }
          std::uint64_t output_end = 0;
          if (!checked_add(output_bytes, length, &output_end)) {
            return finish(DecodeFailureKind::kInternal,
                          "output range overflow");
          }
          ++token_count;
          if (!sink.on_token(TokenKind::kLengthDistance, begin, reader.pos(),
                             output_bytes, output_end, 0,
                             static_cast<std::uint16_t>(length),
                             static_cast<std::uint16_t>(distance), src,
                             source_end, symbol)) {
            return finish(DecodeFailureKind::kObserverStop, std::string());
          }
          if (!sink.emit_match_bytes(output_bytes,
                                     static_cast<std::uint16_t>(length),
                                     static_cast<std::uint16_t>(distance))) {
            return finish(DecodeFailureKind::kInternal, sink.last_error());
          }
          output_bytes = output_end;
        } else {
          return finish(DecodeFailureKind::kMalformed,
                        "invalid literal/length symbol");
        }
      }
    } else {
      return finish(DecodeFailureKind::kMalformed,
                    "reserved deflate block type");
    }

    if (bfinal != 0) {
      done = true;
    }
  }

  outcome.stream_ended = true;
  outcome.input_bits = reader.pos();
  outcome.output_bytes = output_bytes;
  outcome.token_count = token_count;
  return outcome;
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

  const std::uint64_t start_byte = wrapper.deflate_data_begin;
  if (!wrapper.adler_offset || *wrapper.adler_offset < start_byte) {
    out.error = "invalid zlib data range";
    return out;
  }

  RichSink sink;
  const DecodeOutcome outcome =
      run_deflate_blocks(source, start_byte, *wrapper.adler_offset,
                         max_output_bytes, {}, DecodeBudget{}, sink);
  out = std::move(sink.result);
  out.deflate_data_begin = start_byte;
  out.output_bytes = outcome.output_bytes;
  if (outcome.failure == DecodeFailureKind::kNone) {
    out.success = true;
    out.stream_ended = true;
  } else {
    out.success = false;
    out.stream_ended = false;
    out.error = outcome.error;
  }
  return out;
}

TokenScanResult scan_tokens(const pnga::io::IByteSource& source,
                            const TokenScanOptions& options) {
  TokenScanResult result;
  const ZlibWrapperTrace wrapper = trace_zlib_wrapper(source);
  if (!wrapper.success) {
    result.status = TokenScanStatus::kInvalidInput;
    result.error = wrapper.error;
    return result;
  }
  if (wrapper.fdict) {
    result.status = TokenScanStatus::kInvalidInput;
    result.error = "preset dictionaries (FDICT) are not supported";
    return result;
  }
  const std::uint64_t start_byte = wrapper.deflate_data_begin;
  if (!wrapper.adler_offset || *wrapper.adler_offset < start_byte) {
    result.status = TokenScanStatus::kInvalidInput;
    result.error = "invalid zlib data range";
    return result;
  }

  // 0 means unlimited for the scalar scan.
  const std::uint64_t effective_max_output =
      options.max_output_bytes == 0
          ? std::numeric_limits<std::uint64_t>::max()
          : options.max_output_bytes;
  DecodeBudget budget;
  budget.enabled = true;
  budget.max_input_bytes = options.max_input_bytes;
  budget.max_tokens = options.max_tokens;

  ScalarSink sink(options.observer);
  const DecodeOutcome outcome =
      run_deflate_blocks(source, start_byte, *wrapper.adler_offset,
                         effective_max_output, options.should_cancel, budget,
                         sink);
  result.token_count = outcome.token_count;
  result.input_bits = outcome.input_bits;
  result.output_bytes = outcome.output_bytes;
  result.peak_retained_token_records = sink.peak_retained_token_records;
  switch (outcome.failure) {
    case DecodeFailureKind::kNone:
      result.status = TokenScanStatus::kReady;
      result.stream_ended = true;
      break;
    case DecodeFailureKind::kObserverStop:
      result.status = TokenScanStatus::kPartial;
      break;
    case DecodeFailureKind::kCancelled:
      result.status = TokenScanStatus::kCancelled;
      result.error = outcome.error;
      break;
    case DecodeFailureKind::kBudget:
      result.status = TokenScanStatus::kBudgetExceeded;
      result.error = outcome.error;
      break;
    case DecodeFailureKind::kMalformed:
      result.status = TokenScanStatus::kInvalidInput;
      result.error = outcome.error;
      break;
    case DecodeFailureKind::kIo:
    case DecodeFailureKind::kInternal:
      result.status = TokenScanStatus::kError;
      result.error = outcome.error;
      break;
  }
  return result;
}

}  // namespace pnga::deflate_trace
