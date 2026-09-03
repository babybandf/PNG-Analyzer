// WP-607C corpus generator (test-only): materializes the 19-case controlled
// registry as PNG files plus a deterministic index.json catalog under a
// build-tree directory. The generator never calls production parsers, uses no
// clock/randomness/host input, refuses destinations inside the source corpus
// directory and replaces existing output atomically (staging directory first,
// then rename, keeping the last complete output on any earlier failure).

#include "controlled_fixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef PNGA_WP607C_CORPUS_REVISION
#error "PNGA_WP607C_CORPUS_REVISION must be defined by the build"
#endif
#ifndef PNGA_WP607C_SOURCE_CORPUS_DIR
#error "PNGA_WP607C_SOURCE_CORPUS_DIR must be defined by the build"
#endif

namespace fs = std::filesystem;

namespace {

using pnga_test::wp607c::BlockKind;
using pnga_test::wp607c::ByteRangeFact;
using pnga_test::wp607c::ControlledCaseId;
using pnga_test::wp607c::ControlledFixture;
using pnga_test::wp607c::ErrorFacts;
using pnga_test::wp607c::ExpectedFacts;
using pnga_test::wp607c::ImageFacts;
using pnga_test::wp607c::TokenKind;

// --- checked arithmetic (AGENTS.md) ------------------------------------------

std::size_t checked_stream_size(std::uint64_t size) {
  if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::invalid_argument("WP-607C generator size overflows size_t");
  }
  return static_cast<std::size_t>(size);
}

// --- SHA-256 (FIPS 180-4), private to this test-only file ---------------------

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t rotr(std::uint32_t value, unsigned count) {
  return (value >> count) | (value << (32 - count));
}

class Sha256 {
 public:
  void update(const std::byte* data, std::size_t length) {
    const std::uint64_t added_bits =
        static_cast<std::uint64_t>(length) * 8ull;
    if (length > 0 &&
        added_bits / 8ull != static_cast<std::uint64_t>(length)) {
      throw std::invalid_argument("WP-607C SHA-256 input length overflows");
    }
    if (added_bits > std::numeric_limits<std::uint64_t>::max() - total_bits_) {
      throw std::invalid_argument("WP-607C SHA-256 input exceeds 2^64 bits");
    }
    total_bits_ += added_bits;
    while (length > 0) {
      const std::size_t fill = std::min(length, kBlockBytes - pending_);
      for (std::size_t i = 0; i < fill; ++i) {
        buffer_[pending_ + i] = static_cast<std::uint8_t>(data[i]);
      }
      pending_ += fill;
      data += fill;
      length -= fill;
      if (pending_ == kBlockBytes) {
        process_block();
        pending_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> digest() {
    // Checked message padding (FIPS 180-4 §5.1.1): 0x80, zeros to 56 mod 64,
    // then the 64-bit big-endian message bit length. Padding bytes are not
    // counted in total_bits_; update() leaves pending_ < 64 here.
    const std::uint64_t message_bits = total_bits_;
    buffer_[pending_++] = 0x80;
    if (pending_ == kBlockBytes) {
      process_block();
      pending_ = 0;
    }
    while (pending_ != 56) {
      buffer_[pending_++] = 0;
      if (pending_ == kBlockBytes) {
        process_block();
        pending_ = 0;
      }
    }
    for (unsigned shift = 0; shift < 64; shift += 8) {
      buffer_[pending_++] = static_cast<std::uint8_t>(
          (message_bits >> (56 - shift)) & 0xFFu);
    }
    process_block();
    pending_ = 0;

    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i) {
      out[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
      out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
      out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
      out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFu);
    }
    return out;
  }

 private:
  static constexpr std::size_t kBlockBytes = 64;

  void process_block() {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(buffer_[i * 4]) << 24) |
             (static_cast<std::uint32_t>(buffer_[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(buffer_[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(buffer_[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = h + s1 + ch + kSha256RoundConstants[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, kBlockBytes> buffer_{};
  std::size_t pending_ = 0;
  std::uint64_t total_bits_ = 0;
};

std::string sha256_hex(std::span<const std::byte> bytes) {
  Sha256 hash;
  if (!bytes.empty()) {
    hash.update(bytes.data(), bytes.size());
  }
  const auto raw = hash.digest();
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(raw.size() * 2);
  for (const std::uint8_t byte : raw) {
    out.push_back(kHex[(byte >> 4) & 0xFu]);
    out.push_back(kHex[byte & 0xFu]);
  }
  return out;
}

// --- JSON writing (compact, deterministic, UTF-8, no absolute paths) ----------

void json_escape(std::string& out, std::string_view text) {
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          static constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xFu]);
          out.push_back(kHex[static_cast<unsigned char>(c) & 0xFu]);
        } else {
          out.push_back(c);
        }
    }
  }
}

void json_string(std::string& out, std::string_view text) {
  out.push_back('"');
  json_escape(out, text);
  out.push_back('"');
}

void json_number(std::string& out, std::uint64_t value) {
  out += std::to_string(value);
}

void json_array_numbers(std::string& out,
                        const std::vector<std::uint8_t>& values) {
  out.push_back('[');
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    json_number(out, values[i]);
  }
  out.push_back(']');
}

void json_range(std::string& out, const ByteRangeFact& range) {
  out.push_back('[');
  json_number(out, range.begin);
  out.push_back(',');
  json_number(out, range.end);
  out.push_back(']');
}

// --- expected-facts serialization (typed facts -> deterministic JSON) ---------

void append_image_facts(std::string& out, const ImageFacts& image) {
  out.push_back('{');
  auto comma = [&out, first = true]() mutable {
    if (!first) {
      out.push_back(',');
    }
    first = false;
  };
  if (!image.alpha_entries.empty()) {
    comma();
    json_string(out, "alpha_entries");
    out.push_back(':');
    json_array_numbers(out, image.alpha_entries);
  }
  comma();
  json_string(out, "bit_depth");
  out.push_back(':');
  json_number(out, image.bit_depth);
  comma();
  json_string(out, "color_type");
  out.push_back(':');
  json_number(out, image.color_type);
  if (!image.empty_passes.empty()) {
    comma();
    json_string(out, "empty_passes");
    out.push_back(':');
    json_array_numbers(out, image.empty_passes);
  }
  comma();
  json_string(out, "height");
  out.push_back(':');
  json_number(out, image.height);
  comma();
  json_string(out, "interlace");
  out.push_back(':');
  json_number(out, image.interlace);
  if (!image.palette_entries.empty()) {
    comma();
    json_string(out, "palette_entries");
    out.push_back(':');
    out.push_back('[');
    for (std::size_t i = 0; i < image.palette_entries.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      json_array_numbers(out, std::vector<std::uint8_t>(
                                  image.palette_entries[i].begin(),
                                  image.palette_entries[i].end()));
    }
    out.push_back(']');
  }
  // Row-filter lists stay exact for the small images; the large performance
  // case (all-None rows) omits the constant bulk from the catalog.
  if (!image.row_filters.empty() && image.row_filters.size() <= 16) {
    comma();
    json_string(out, "row_filters");
    out.push_back(':');
    json_array_numbers(out, image.row_filters);
  }
  if (!image.selected_sample_bytes.empty()) {
    comma();
    json_string(out, "selected_sample_bytes");
    out.push_back(':');
    json_array_numbers(out, image.selected_sample_bytes);
  }
  comma();
  json_string(out, "width");
  out.push_back(':');
  json_number(out, image.width);
  out.push_back('}');
}

std::string_view block_kind_name(BlockKind kind) {
  switch (kind) {
    case BlockKind::kStored:
      return "stored";
    case BlockKind::kFixed:
      return "fixed";
    case BlockKind::kDynamic:
      return "dynamic";
    case BlockKind::kReserved:
      break;
  }
  throw std::invalid_argument("WP-607C catalog block kind unsupported");
}

std::string_view token_kind_name(TokenKind kind) {
  switch (kind) {
    case TokenKind::kLiteral:
      return "literal";
    case TokenKind::kMatch:
      return "match";
    case TokenKind::kEndOfBlock:
      return "end_of_block";
  }
  throw std::invalid_argument("WP-607C catalog token kind unsupported");
}

void append_block_fact(std::string& out, const pnga_test::wp607c::BlockFact& b) {
  out.push_back('{');
  json_string(out, "bfinal");
  out.push_back(':');
  out += b.bfinal ? "true" : "false";
  out.push_back(',');
  json_string(out, "input_bits");
  out.push_back(':');
  json_range(out, b.input_bits);
  out.push_back(',');
  json_string(out, "kind");
  out.push_back(':');
  json_string(out, block_kind_name(b.kind));
  out.push_back(',');
  json_string(out, "output_bytes");
  out.push_back(':');
  json_range(out, b.output_bytes);
  out.push_back('}');
}

void append_token_fact(std::string& out, const pnga_test::wp607c::TokenFact& t) {
  out.push_back('{');
  bool first = true;
  auto comma = [&]() {
    if (!first) {
      out.push_back(',');
    }
    first = false;
  };
  if (t.distance.has_value()) {
    comma();
    json_string(out, "distance");
    out.push_back(':');
    json_number(out, *t.distance);
  }
  comma();
  json_string(out, "input_bits");
  out.push_back(':');
  json_range(out, t.input_bits);
  comma();
  json_string(out, "kind");
  out.push_back(':');
  json_string(out, token_kind_name(t.kind));
  if (t.length.has_value()) {
    comma();
    json_string(out, "length");
    out.push_back(':');
    json_number(out, *t.length);
  }
  if (t.literal.has_value()) {
    comma();
    json_string(out, "literal");
    out.push_back(':');
    json_number(out, *t.literal);
  }
  if (t.match_source.has_value()) {
    comma();
    json_string(out, "match_source");
    out.push_back(':');
    json_range(out, *t.match_source);
  }
  comma();
  json_string(out, "output_bytes");
  out.push_back(':');
  json_range(out, t.output_bytes);
  out.push_back('}');
}

void append_error_facts(std::string& out, const ErrorFacts& error) {
  out.push_back('{');
  bool first = true;
  auto comma = [&]() {
    if (!first) {
      out.push_back(',');
    }
    first = false;
  };
  if (error.decoder_message.has_value()) {
    comma();
    json_string(out, "decoder_message");
    out.push_back(':');
    json_string(out, *error.decoder_message);
  }
  if (error.stop_input_bit.has_value()) {
    comma();
    json_string(out, "stop_input_bit");
    out.push_back(':');
    json_number(out, *error.stop_input_bit);
  }
  if (error.stop_output_byte.has_value()) {
    comma();
    json_string(out, "stop_output_byte");
    out.push_back(':');
    json_number(out, *error.stop_output_byte);
  }
  if (error.validation_rule_id.has_value()) {
    comma();
    json_string(out, "validation_rule_id");
    out.push_back(':');
    json_string(out, *error.validation_rule_id);
  }
  out.push_back('}');
}

void append_expected_facts(std::string& out, const ExpectedFacts& facts) {
  out.push_back('{');
  bool first = true;
  auto comma = [&]() {
    if (!first) {
      out.push_back(',');
    }
    first = false;
  };
  if (!facts.blocks.empty()) {
    comma();
    json_string(out, "blocks");
    out.push_back(':');
    out.push_back('[');
    for (std::size_t i = 0; i < facts.blocks.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      append_block_fact(out, facts.blocks[i]);
    }
    out.push_back(']');
  }
  if (facts.error.has_value()) {
    comma();
    json_string(out, "error");
    out.push_back(':');
    append_error_facts(out, *facts.error);
  }
  if (!facts.expected_code_length_repeats.empty()) {
    comma();
    json_string(out, "expected_code_length_repeats");
    out.push_back(':');
    json_array_numbers(out, facts.expected_code_length_repeats);
  }
  if (facts.image.has_value()) {
    comma();
    json_string(out, "image");
    out.push_back(':');
    append_image_facts(out, *facts.image);
  }
  if (!facts.physical_spans.empty()) {
    comma();
    json_string(out, "physical_spans");
    out.push_back(':');
    out.push_back('[');
    for (std::size_t i = 0; i < facts.physical_spans.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      json_range(out, facts.physical_spans[i]);
    }
    out.push_back(']');
  }
  if (!facts.tokens.empty()) {
    comma();
    json_string(out, "tokens");
    out.push_back(':');
    out.push_back('[');
    for (std::size_t i = 0; i < facts.tokens.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      append_token_fact(out, facts.tokens[i]);
    }
    out.push_back(']');
  }
  out.push_back('}');
}

// --- frozen per-case catalog metadata (package §7) ----------------------------

struct CaseMeta {
  const char* expected_class;
  const char* output;
  std::array<const char*, 7> features;
  std::size_t feature_count;
  const char* linked_test;
};

const CaseMeta& meta_for(ControlledCaseId id) {
  switch (id) {
    case ControlledCaseId::kUiGray1None: {
      static const CaseMeta meta{
          "ui", "valid/",
          {"bit_depth_1", "grayscale", "non_interlaced", "", "", "", ""},
          3, "wp607c_png_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kUiIndexed4Trns: {
      static const CaseMeta meta{
          "ui", "valid/",
          {"bit_depth_4", "indexed_color", "non_interlaced", "plte", "trns",
           "", ""},
          5, "wp607c_png_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kUiRgb8FiveFilters: {
      static const CaseMeta meta{
          "ui", "valid/",
          {"filter_average", "filter_none", "filter_paeth", "filter_sub",
           "filter_up", "non_interlaced", "rgb8"},
          7, "wp607c_png_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kUiRgba16ByteSelect: {
      static const CaseMeta meta{
          "ui", "valid/",
          {"big_endian_samples", "bit_depth_16", "non_interlaced", "rgba16",
           "", "", ""},
          4, "wp607c_png_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kUiAdam7EmptyPasses: {
      static const CaseMeta meta{
          "ui", "valid/",
          {"adam7", "empty_passes", "interlaced", "rgba8", "", "", ""},
          4, "wp607c_png_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kTraceStoredLiterals: {
      static const CaseMeta meta{
          "valid", "valid/",
          {"bfinal", "eob", "stored_block", "", "", "", ""},
          3, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kTraceFixedNonoverlap: {
      static const CaseMeta meta{
          "valid", "valid/",
          {"bfinal", "eob", "fixed_huffman", "literal", "match_nonoverlap",
           "", ""},
          5, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kTraceDynamicOverlapRepeats: {
      static const CaseMeta meta{
          "valid", "valid/",
          {"bfinal", "dynamic_huffman", "eob", "literal", "match_overlap",
           "repeats_16_17_18", ""},
          6, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kTraceMultiblockBfinal: {
      static const CaseMeta meta{
          "valid", "valid/",
          {"bfinal_false_false_true", "dynamic_huffman", "fixed_huffman",
           "stored_block", "three_blocks", "", ""},
          5, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kIdatSplitZlibHeader: {
      static const CaseMeta meta{
          "boundary", "valid/",
          {"cross_idat", "two_spans", "zlib_header_split", "", "", "", ""},
          3, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kIdatSplitToken: {
      static const CaseMeta meta{
          "boundary", "valid/",
          {"cross_idat", "token_span", "two_spans", "", "", "", ""},
          3, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kIdatSplitAdler: {
      static const CaseMeta meta{
          "boundary", "valid/",
          {"adler_two_spans", "cross_idat", "two_spans", "", "", "", ""},
          3, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorTruncatedHeader: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"malformed", "stop_input_bit_16", "stop_output_byte_0",
           "truncated_block_header", "", "", ""},
          4, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorTruncatedToken: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"malformed", "stop_input_bit_16", "stop_output_byte_0",
           "truncated_huffman_code", "", "", ""},
          4, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorReservedBtype: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"malformed", "reserved_btype", "stop_input_bit_19",
           "stop_output_byte_0", "", "", ""},
          4, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorInvalidDistance: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"distance_beyond_output", "malformed", "stop_input_bit_16",
           "stop_output_byte_0", "", "", ""},
          4, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorCrcMismatch: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"chunk_crc_mismatch", "malformed", "", "", "", "", ""},
          2, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kErrorAdlerMismatch: {
      static const CaseMeta meta{
          "malformed", "malformed/",
          {"idat_adler_mismatch", "malformed", "", "", "", "", ""},
          2, "wp607c_trace_facts_tests"};
      return meta;
    }
    case ControlledCaseId::kPerfLargeRgba8: {
      static const CaseMeta meta{
          "performance", "valid/",
          {"non_interlaced", "rgba8", "stored_blocks", "", "", "", ""},
          3, "wp607c_trace_facts_tests"};
      return meta;
    }
  }
  throw std::invalid_argument("WP-607C case metadata is incomplete");
}

// Builds one catalog record exactly as the manifest's generated projection.
std::string catalog_record(const ControlledFixture& fixture,
                           const std::string& sha256_hex_value) {
  const CaseMeta& meta = meta_for(fixture.id);
  std::string out;
  out.push_back('{');
  json_string(out, "expected_class");
  out.push_back(':');
  json_string(out, meta.expected_class);
  out.push_back(',');
  json_string(out, "expected_features");
  out.push_back(':');
  out.push_back('[');
  for (std::size_t i = 0; i < meta.feature_count; ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    json_string(out, meta.features[i]);
  }
  out.push_back(']');
  out.push_back(',');
  json_string(out, "expected_facts");
  out.push_back(':');
  append_expected_facts(out, fixture.expected);
  out.push_back(',');
  json_string(out, "expected_sha256");
  out.push_back(':');
  json_string(out, sha256_hex_value);
  out.push_back(',');
  json_string(out, "generator");
  out.push_back(':');
  out += "{\"arguments\":{\"case\":";
  json_string(out, fixture.stable_id);
  out += "},\"case\":";
  json_string(out, fixture.stable_id);
  out += ",\"executable\":\"pnga_generate_wp607c_corpus\"";
  out += ",\"schema_version\":1}";
  out.push_back(',');
  json_string(out, "id");
  out.push_back(':');
  json_string(out, fixture.stable_id);
  out += ",\"kind\":\"generated\"";
  out.push_back(',');
  json_string(out, "linked_tests");
  out.push_back(':');
  out += "[\"";
  out += meta.linked_test;
  out += "\"]";
  out.push_back(',');
  json_string(out, "output");
  out.push_back(':');
  json_string(out, std::string(meta.output) + std::string(fixture.stable_id) +
                       ".png");
  out.push_back('}');
  return out;
}

std::string build_catalog_json(const std::vector<ControlledFixture>& fixtures) {
  std::vector<std::pair<std::string_view, std::string>> records;
  records.reserve(fixtures.size());
  for (const ControlledFixture& fixture : fixtures) {
    records.emplace_back(
        fixture.stable_id,
        catalog_record(fixture,
                       sha256_hex(std::span<const std::byte>(
                           fixture.png_bytes.data(),
                           fixture.png_bytes.size()))));
  }
  std::sort(records.begin(), records.end(),
            [](const auto& left, const auto& right) {
              return left.first < right.first;
            });
  std::string out =
      "{\"cases\":[";
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += records[i].second;
  }
  out += "],\"corpus_revision\":\"" PNGA_WP607C_CORPUS_REVISION "\"";
  out += ",\"schema_version\":1}";
  out.push_back('\n');
  return out;
}

// --- path helpers --------------------------------------------------------------

bool is_inside(const fs::path& child, const fs::path& parent) {
  auto ci = child.begin();
  for (auto pi = parent.begin(); pi != parent.end(); ++pi, ++ci) {
    if (ci == child.end() || *ci != *pi) {
      return false;
    }
  }
  return true;
}

fs::path canonical_path(const fs::path& path) {
  std::error_code error;
  const fs::path resolved = fs::weakly_canonical(path, error);
  if (error) {
    throw std::invalid_argument("WP-607C cannot resolve path: " +
                                path.string());
  }
  return resolved;
}

void write_file(const fs::path& path, std::string_view contents) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::invalid_argument("WP-607C cannot open output file: " +
                                path.string());
  }
  file.write(contents.data(),
             static_cast<std::streamsize>(checked_stream_size(
                 static_cast<std::uint64_t>(contents.size()))));
  file.close();
  if (!file) {
    throw std::invalid_argument("WP-607C failed to write output file: " +
                                path.string());
  }
}

// --- atomic catalog promotion ---------------------------------------------------

[[noreturn]] void fail_generation(const fs::path& staging,
                                  const std::string& message) {
  std::error_code ignored;
  fs::remove_all(staging, ignored);
  throw std::invalid_argument("WP-607C generation failed: " + message);
}

void write_atomic_catalog(const fs::path& output,
                          const std::vector<ControlledFixture>& fixtures) {
  const std::string base = output.string();
  const fs::path staging = base + ".new";
  const fs::path backup = base + ".old";

  std::error_code error;
  fs::remove_all(staging, error);
  if (error) {
    fail_generation(staging, "cannot clear the staging directory");
  }
  const fs::path valid_dir = staging / "valid";
  const fs::path malformed_dir = staging / "malformed";
  fs::create_directories(valid_dir, error);
  if (error) {
    fail_generation(staging, "cannot create the staging valid directory");
  }
  fs::create_directories(malformed_dir, error);
  if (error) {
    fail_generation(staging, "cannot create the staging malformed directory");
  }

  for (const ControlledFixture& fixture : fixtures) {
    const fs::path file = staging /
                          (std::string(meta_for(fixture.id).output) +
                           std::string(fixture.stable_id) + ".png");
    write_file(file, std::string_view(
                         reinterpret_cast<const char*>(fixture.png_bytes.data()),
                         checked_stream_size(
                             static_cast<std::uint64_t>(
                                 fixture.png_bytes.size()))));
  }
  write_file(staging / "index.json", build_catalog_json(fixtures));

  // Promote atomically: move the old output aside, move staging into place,
  // then drop the backup. On a failed promotion restore the last complete
  // output.
  if (fs::exists(fs::symlink_status(output, error)) && !error) {
    fs::rename(output, backup, error);
    if (error) {
      fail_generation(staging, "cannot move the previous output aside");
    }
  }
  error.clear();
  fs::rename(staging, output, error);
  if (error) {
    if (fs::exists(fs::symlink_status(backup, error))) {
      std::error_code restore_error;
      fs::rename(backup, output, restore_error);
      if (restore_error) {
        throw std::invalid_argument(
            "WP-607C generation failed and the previous output could not be "
            "restored: " + restore_error.message());
      }
    }
    std::error_code ignored;
    fs::remove_all(staging, ignored);
    throw std::invalid_argument("WP-607C generation failed: cannot promote " +
                                error.message());
  }
  error.clear();
  fs::remove_all(backup, error);
  if (error) {
    throw std::invalid_argument("WP-607C generation failed: cannot remove " +
                                backup.string());
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::optional<std::string> output;
    std::optional<std::string> sha_text;
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument = argv[i];
      const auto value = [&]() -> std::optional<std::string> {
        if (i + 1 < argc) {
          return std::string{argv[++i]};
        }
        return std::nullopt;
      };
      if (argument == "--output") {
        if (output.has_value()) {
          std::cerr << "usage: pnga_generate_wp607c_corpus --output DIR\n"
                       "       pnga_generate_wp607c_corpus --sha256-text TEXT\n";
          return 2;
        }
        output = value();
        if (!output.has_value()) {
          std::cerr << "pnga_generate_wp607c_corpus: --output needs a value\n";
          return 2;
        }
      } else if (argument == "--sha256-text") {
        if (sha_text.has_value()) {
          std::cerr << "usage: pnga_generate_wp607c_corpus --output DIR\n"
                       "       pnga_generate_wp607c_corpus --sha256-text TEXT\n";
          return 2;
        }
        sha_text = value();
        if (!sha_text.has_value()) {
          std::cerr
              << "pnga_generate_wp607c_corpus: --sha256-text needs a value\n";
          return 2;
        }
      } else {
        std::cerr << "usage: pnga_generate_wp607c_corpus --output DIR\n"
                     "       pnga_generate_wp607c_corpus --sha256-text TEXT\n";
        return 2;
      }
    }

    if (sha_text.has_value()) {
      // Test-only CLI: the validator self-test pins this digest so the
      // private FIPS 180-4 implementation is cross-checked against Python
      // hashlib on every corpus gate run.
      const std::string text = *sha_text;
      std::cout
          << sha256_hex(std::span<const std::byte>(
                 reinterpret_cast<const std::byte*>(text.data()),
                 checked_stream_size(static_cast<std::uint64_t>(
                     text.size()))))
          << "\n";
      return 0;
    }

    if (!output.has_value()) {
      std::cerr << "usage: pnga_generate_wp607c_corpus --output DIR\n"
                   "       pnga_generate_wp607c_corpus --sha256-text TEXT\n";
      return 2;
    }

    static_assert(sizeof(PNGA_WP607C_CORPUS_REVISION) == 65,
                  "the corpus revision must be a 64-hex compile definition");
    const fs::path destination{*output};
    const fs::path source_corpus = canonical_path(PNGA_WP607C_SOURCE_CORPUS_DIR);
    if (is_inside(canonical_path(destination), source_corpus)) {
      std::cerr << "pnga_generate_wp607c_corpus: refusing a destination that "
                   "resolves inside the source corpus directory: "
                << destination.string() << "\n";
      return 2;
    }

    std::vector<ControlledFixture> fixtures;
    fixtures.reserve(pnga_test::wp607c::all_controlled_cases().size());
    for (const ControlledCaseId id : pnga_test::wp607c::all_controlled_cases()) {
      fixtures.push_back(pnga_test::wp607c::make_controlled_fixture(id));
    }
    write_atomic_catalog(destination, fixtures);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "pnga_generate_wp607c_corpus: " << error.what() << "\n";
    return 1;
  }
}
