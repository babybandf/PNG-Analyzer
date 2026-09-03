#include "controlled_fixture.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace pnga_test::wp607c {
namespace {

constexpr std::size_t kMaxStoredBlockBytes = 65535;

std::byte B(unsigned int value) { return static_cast<std::byte>(value & 0xFFu); }

constexpr std::byte CB(unsigned int value) {
  return static_cast<std::byte>(value & 0xFFu);
}

std::byte C(char value) {
  return static_cast<std::byte>(static_cast<unsigned char>(value));
}

std::uint8_t U(std::byte value) { return std::to_integer<std::uint8_t>(value); }

// --- checked arithmetic (AGENTS.md: every conversion/growth is checked) -----

std::optional<std::size_t> checked_size(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(value);
}

bool checked_append_size(std::size_t current, std::size_t addition,
                         std::size_t& result) {
  if (addition > std::numeric_limits<std::size_t>::max() - current) {
    return false;
  }
  result = current + addition;
  return true;
}

// --- big-endian integer and chunk writers ----------------------------------

void append_u32_be(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(B(value >> 24));
  out.push_back(B(value >> 16));
  out.push_back(B(value >> 8));
  out.push_back(B(value));
}

std::uint32_t chunk_crc(std::string_view type, std::span<const std::byte> data) {
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(type.data()),
              static_cast<uInt>(type.size()));
  if (!data.empty()) {
    if (data.size() > std::numeric_limits<uInt>::max()) {
      throw std::invalid_argument("WP-607C chunk payload exceeds the zlib API limit");
    }
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                static_cast<uInt>(data.size()));
  }
  return static_cast<std::uint32_t>(crc);
}

void push_chunk(std::vector<std::byte>& png, std::string_view type,
                std::span<const std::byte> data) {
  append_u32_be(png, static_cast<std::uint32_t>(data.size()));
  for (const char c : type) {
    png.push_back(C(c));
  }
  png.insert(png.end(), data.begin(), data.end());
  append_u32_be(png, chunk_crc(type, data));
}

// --- deterministic Stored-Block zlib wrapper (RFC 1950/1951) ----------------

// Wraps `filtered` in a zlib stream made only of final Stored blocks: 2-byte
// header, then per block one byte holding BFINAL/BTYPE=00 with padding, a
// little-endian LEN/NLEN pair, the raw bytes, and the big-endian Adler-32.
std::vector<std::byte> make_stored_zlib(std::span<const std::byte> filtered) {
  std::vector<std::byte> out;
  out.push_back(B(0x78));  // CMF: deflate, 32 KiB window
  out.push_back(B(0x01));  // FLG: (0x7801 % 31) == 0, no preset dictionary
  std::size_t offset = 0;
  do {
    const std::size_t remaining = filtered.size() - offset;
    const std::size_t chunk = std::min(remaining, kMaxStoredBlockBytes);
    const bool final_block = offset + chunk == filtered.size();
    // BFINAL in bit 0, BTYPE=00 in bits 1-2, byte padding in bits 3-7.
    out.push_back(B(final_block ? 0x01u : 0x00u));
    const std::uint16_t len = static_cast<std::uint16_t>(chunk);
    const std::uint16_t nlen = static_cast<std::uint16_t>(~len & 0xFFFFu);
    out.push_back(B(len & 0xFFu));           // LEN/NLEN are little-endian
    out.push_back(B(len >> 8));
    out.push_back(B(nlen & 0xFFu));
    out.push_back(B(nlen >> 8));
    out.insert(out.end(), filtered.begin() + static_cast<std::ptrdiff_t>(offset),
               filtered.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
    offset += chunk;
  } while (offset < filtered.size());
  if (filtered.size() > std::numeric_limits<uInt>::max()) {
    throw std::invalid_argument("WP-607C stored stream exceeds the zlib API limit");
  }
  const uLong adler =
      adler32(adler32(0L, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(filtered.data()),
              static_cast<uInt>(filtered.size()));
  append_u32_be(out, static_cast<std::uint32_t>(adler));
  return out;
}

// --- forward PNG filter encoder (RGB8/RGBA8 rows, filters 0-4) --------------

std::uint8_t paeth_predictor(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) {
    return static_cast<std::uint8_t>(a);
  }
  if (pb <= pc) {
    return static_cast<std::uint8_t>(b);
  }
  return static_cast<std::uint8_t>(c);
}

std::vector<std::byte> forward_filter_rows(
    const std::vector<std::vector<std::byte>>& raw_rows,
    const std::vector<std::uint8_t>& filters, std::size_t bpp) {
  if (raw_rows.size() != filters.size()) {
    throw std::invalid_argument("WP-607C filter plan does not match the rows");
  }
  std::size_t total = 0;
  for (const auto& row : raw_rows) {
    if (!checked_append_size(total, row.size() + 1, total)) {
      throw std::invalid_argument("WP-607C filtered size overflows");
    }
  }
  std::vector<std::byte> out;
  out.reserve(total);
  const auto at = [](const std::vector<std::byte>& row, std::size_t x) -> int {
    return x < row.size() ? static_cast<int>(U(row[x])) : 0;
  };
  for (std::size_t y = 0; y < raw_rows.size(); ++y) {
    const auto& row = raw_rows[y];
    const auto& prior = y > 0 ? raw_rows[y - 1] : raw_rows[0];
    const bool has_prior = y > 0;
    out.push_back(B(filters[y]));
    for (std::size_t x = 0; x < row.size(); ++x) {
      const int raw = static_cast<int>(U(row[x]));
      const int left = x >= bpp ? at(row, x - bpp) : 0;
      const int up = has_prior ? at(prior, x) : 0;
      const int up_left = x >= bpp && has_prior ? at(prior, x - bpp) : 0;
      int predictor = 0;
      switch (filters[y]) {
        case 0:
          break;
        case 1:
          predictor = left;
          break;
        case 2:
          predictor = up;
          break;
        case 3:
          predictor = (left + up) / 2;
          break;
        case 4:
          predictor = paeth_predictor(left, up, up_left);
          break;
        default:
          throw std::invalid_argument("WP-607C unsupported filter type");
      }
      out.push_back(B(static_cast<unsigned>(raw - predictor) & 0xFFu));
    }
  }
  return out;
}

// --- Adam7 pass geometry -----------------------------------------------------

struct Adam7Pass {
  std::uint32_t x_start;
  std::uint32_t y_start;
  std::uint32_t x_step;
  std::uint32_t y_step;
};

constexpr Adam7Pass kAdam7Passes[7] = {
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
    {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2},
};

// Returns the zero-based indexes of the Adam7 passes that carry no pixels.
std::vector<std::uint8_t> adam7_empty_passes(std::uint32_t width,
                                             std::uint32_t height) {
  std::vector<std::uint8_t> empty;
  for (unsigned pass = 0; pass < 7; ++pass) {
    const auto& geo = kAdam7Passes[pass];
    const bool has_pixels = width > geo.x_start && height > geo.y_start;
    if (!has_pixels) {
      empty.push_back(static_cast<std::uint8_t>(pass));
    }
  }
  return empty;
}

// --- shared PNG assembly -----------------------------------------------------

std::vector<std::byte> ihdr_data(std::uint32_t width, std::uint32_t height,
                                 std::uint8_t bit_depth, std::uint8_t color_type,
                                 std::uint8_t interlace) {
  std::vector<std::byte> data;
  data.reserve(13);
  append_u32_be(data, width);
  append_u32_be(data, height);
  data.push_back(B(bit_depth));
  data.push_back(B(color_type));
  data.push_back(B(0));  // compression method: deflate
  data.push_back(B(0));  // filter method: adaptive per row
  data.push_back(B(interlace));
  return data;
}

void begin_png(std::vector<std::byte>& png, std::uint32_t width,
               std::uint32_t height, std::uint8_t bit_depth,
               std::uint8_t color_type, std::uint8_t interlace) {
  static constexpr std::array<std::byte, 8> kSignature = {
      CB(0x89), CB(0x50), CB(0x4E), CB(0x47),
      CB(0x0D), CB(0x0A), CB(0x1A), CB(0x0A)};
  png.insert(png.end(), kSignature.begin(), kSignature.end());
  push_chunk(png, "IHDR",
             ihdr_data(width, height, bit_depth, color_type, interlace));
}

std::vector<std::byte> finish_png(std::vector<std::byte> png,
                                  const std::vector<std::byte>& idat) {
  push_chunk(png, "IDAT", idat);
  push_chunk(png, "IEND", {});
  return png;
}

std::vector<std::byte> packed_gray1_rows(std::uint32_t width,
                                         std::uint32_t height) {
  const auto value = [](std::uint32_t x, std::uint32_t y) {
    return (x + y) % 2;
  };
  const std::size_t row_bytes = (width + 7) / 8;
  const auto row_bytes_checked = checked_size(static_cast<std::uint64_t>(row_bytes));
  if (!row_bytes_checked) {
    throw std::invalid_argument("WP-607C row size overflows");
  }
  std::vector<std::byte> filtered;
  for (std::uint32_t y = 0; y < height; ++y) {
    filtered.push_back(B(0));  // filter None
    for (std::size_t byte_index = 0; byte_index < *row_bytes_checked;
         ++byte_index) {
      unsigned packed = 0;
      for (unsigned bit = 0; bit < 8; ++bit) {
        const std::uint64_t x = static_cast<std::uint64_t>(byte_index) * 8 + bit;
        if (x < width && value(static_cast<std::uint32_t>(x), y) != 0) {
          packed |= 0x80u >> bit;  // leftmost pixel in the MSB
        }
      }
      filtered.push_back(B(packed));
    }
  }
  return filtered;
}

// --- the five pixel case builders -------------------------------------------

ControlledFixture make_ui_gray1_none() {
  const std::uint32_t width = 9;
  const std::uint32_t height = 3;
  std::vector<std::byte> png;
  begin_png(png, width, height, 1, 0, 0);
  const std::vector<std::byte> filtered = packed_gray1_rows(width, height);
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kUiGray1None;
  fixture.stable_id = "ui-gray1-none";
  fixture.png_bytes = finish_png(std::move(png), make_stored_zlib(filtered));
  ImageFacts facts;
  facts.width = width;
  facts.height = height;
  facts.bit_depth = 1;
  facts.color_type = 0;
  facts.interlace = 0;
  facts.row_filters = {0, 0, 0};
  fixture.expected.image = std::move(facts);
  return fixture;
}

ControlledFixture make_ui_indexed4_trns() {
  const std::uint32_t width = 5;
  const std::uint32_t height = 3;
  ImageFacts facts;
  facts.width = width;
  facts.height = height;
  facts.bit_depth = 4;
  facts.color_type = 3;
  facts.interlace = 0;
  facts.row_filters = {0, 0, 0};
  for (unsigned i = 0; i < 16; ++i) {
    facts.palette_entries.push_back(
        {static_cast<std::uint8_t>(i * 17), static_cast<std::uint8_t>(255 - i * 17),
         static_cast<std::uint8_t>(i * 8 + 3)});
    facts.alpha_entries.push_back(static_cast<std::uint8_t>(i * 15));
  }

  std::vector<std::byte> png;
  begin_png(png, width, height, 4, 3, 0);
  std::vector<std::byte> plte;
  for (const auto& entry : facts.palette_entries) {
    plte.push_back(B(entry[0]));
    plte.push_back(B(entry[1]));
    plte.push_back(B(entry[2]));
  }
  push_chunk(png, "PLTE", plte);
  std::vector<std::byte> trns;
  trns.reserve(facts.alpha_entries.size());
  for (const std::uint8_t alpha : facts.alpha_entries) {
    trns.push_back(B(alpha));
  }
  push_chunk(png, "tRNS", trns);

  // index(x, y) = x + 5y, packed two nibbles per byte, MSB first.
  const std::vector<std::byte> filtered = {
      B(0x00), B(0x01), B(0x23), B(0x40),
      B(0x00), B(0x56), B(0x78), B(0x90),
      B(0x00), B(0xAB), B(0xCD), B(0xE0),
  };
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kUiIndexed4Trns;
  fixture.stable_id = "ui-indexed4-trns";
  fixture.png_bytes = finish_png(std::move(png), make_stored_zlib(filtered));
  fixture.expected.image = std::move(facts);
  return fixture;
}

ControlledFixture make_ui_rgb8_five_filters() {
  const std::uint32_t width = 8;
  const std::uint32_t height = 5;
  std::vector<std::vector<std::byte>> raw_rows;
  const auto row_of = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::byte> row;
    for (unsigned x = 0; x < 8; ++x) {
      row.push_back(B(r));
      row.push_back(B(g));
      row.push_back(B(b));
    }
    return row;
  };
  raw_rows.push_back(row_of(0x00, 0x11, 0x22));
  raw_rows.push_back(row_of(0x10, 0x20, 0x30));
  raw_rows.push_back(row_of(0x10, 0x20, 0x30));
  raw_rows.push_back(row_of(0x20, 0x20, 0x20));
  raw_rows.push_back(row_of(0x20, 0x20, 0x20));
  const std::vector<std::uint8_t> filters = {0, 1, 2, 3, 4};
  const std::vector<std::byte> filtered =
      forward_filter_rows(raw_rows, filters, 3);

  std::vector<std::byte> png;
  begin_png(png, width, height, 8, 2, 0);
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kUiRgb8FiveFilters;
  fixture.stable_id = "ui-rgb8-five-filters";
  fixture.png_bytes = finish_png(std::move(png), make_stored_zlib(filtered));
  ImageFacts facts;
  facts.width = width;
  facts.height = height;
  facts.bit_depth = 8;
  facts.color_type = 2;
  facts.interlace = 0;
  facts.row_filters = filters;
  fixture.expected.image = std::move(facts);
  return fixture;
}

ControlledFixture make_ui_rgba16_byte_select() {
  const std::uint32_t width = 3;
  const std::uint32_t height = 2;
  const std::array<unsigned, 4> kBase = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
  std::vector<std::byte> filtered;
  for (std::uint32_t y = 0; y < height; ++y) {
    filtered.push_back(B(0));  // filter None
    for (std::uint32_t x = 0; x < width; ++x) {
      const unsigned offset = x + width * y;
      for (const unsigned base : kBase) {
        const unsigned value = base + offset;
        filtered.push_back(B(value >> 8));  // big-endian sample bytes
        filtered.push_back(B(value));
      }
    }
  }

  std::vector<std::byte> png;
  begin_png(png, width, height, 16, 6, 0);
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kUiRgba16ByteSelect;
  fixture.stable_id = "ui-rgba16-byte-select";
  fixture.png_bytes = finish_png(std::move(png), make_stored_zlib(filtered));
  ImageFacts facts;
  facts.width = width;
  facts.height = height;
  facts.bit_depth = 16;
  facts.color_type = 6;
  facts.interlace = 0;
  facts.row_filters = {0, 0};
  // The selected sample is pixel (0,0) red: 0x1234 big-endian.
  facts.selected_sample_bytes = {0x12, 0x34};
  fixture.expected.image = std::move(facts);
  return fixture;
}

ControlledFixture make_ui_adam7_empty_passes() {
  const std::uint32_t width = 2;
  const std::uint32_t height = 1;
  const std::byte pixel0[4] = {B(0x10), B(0x20), B(0x30), B(0x40)};
  const std::byte pixel1[4] = {B(0x50), B(0x60), B(0x70), B(0x80)};
  std::vector<std::byte> filtered;
  std::vector<std::uint8_t> row_filters;
  for (unsigned pass = 0; pass < 7; ++pass) {
    const auto& geo = kAdam7Passes[pass];
    const bool has_pixels = width > geo.x_start && height > geo.y_start;
    if (!has_pixels) {
      continue;
    }
    // Exactly one pixel in this pass for the fixed 2x1 geometry.
    const std::byte* pixel = pass == 0 ? pixel0 : pixel1;
    filtered.push_back(B(0));  // filter None for every pass scanline
    row_filters.push_back(0);
    filtered.insert(filtered.end(), pixel, pixel + 4);
  }

  std::vector<std::byte> png;
  begin_png(png, width, height, 8, 6, 1);
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kUiAdam7EmptyPasses;
  fixture.stable_id = "ui-adam7-empty-passes";
  fixture.png_bytes = finish_png(std::move(png), make_stored_zlib(filtered));
  ImageFacts facts;
  facts.width = width;
  facts.height = height;
  facts.bit_depth = 8;
  facts.color_type = 6;
  facts.interlace = 1;
  facts.row_filters = std::move(row_filters);
  facts.empty_passes = adam7_empty_passes(width, height);
  fixture.expected.image = std::move(facts);
  return fixture;
}

// --- explicit LSB-first DEFLATE bit writing (RFC 1951 §3.1.1) ---------------

class BitWriter {
 public:
  std::uint64_t bit_position() const noexcept { return bit_position_; }

  // Writes `count` (<= 32) bits of `value` starting from its LSB.
  void write_lsb(std::uint32_t value, unsigned count) {
    if (count > 32) {
      throw std::invalid_argument("WP-607C bit write exceeds 32 bits");
    }
    if (bit_position_ >
        std::numeric_limits<std::uint64_t>::max() - count) {
      throw std::invalid_argument("WP-607C stream bit position overflows");
    }
    for (unsigned i = 0; i < count; ++i) {
      if (bit_position_ % 8 == 0) {
        bytes_.push_back(B(0));
      }
      if (((value >> i) & 1u) != 0) {
        const unsigned shift = static_cast<unsigned>(bit_position_ % 8);
        bytes_.back() = B((static_cast<unsigned>(U(bytes_.back())) |
                           (1u << shift)) & 0xFFu);
      }
      ++bit_position_;
    }
  }

  // Writes a canonical Huffman code MSB-first (RFC 1951 §3.1.1).
  void write_canonical(std::uint32_t code, unsigned count) {
    if (count == 0 || count > 16) {
      throw std::invalid_argument("WP-607C invalid Huffman code length");
    }
    for (unsigned i = 0; i < count; ++i) {
      write_lsb((code >> (count - 1 - i)) & 1u, 1);
    }
  }

  void align_to_byte() {
    const unsigned remainder = static_cast<unsigned>(bit_position_ % 8);
    if (remainder != 0) {
      write_lsb(0, 8 - remainder);
    }
  }

  std::span<const std::byte> bytes() const noexcept {
    return std::span<const std::byte>(bytes_.data(), bytes_.size());
  }

 private:
  std::vector<std::byte> bytes_;
  std::uint64_t bit_position_ = 0;
};

constexpr std::uint64_t kZlibHeaderBits = 16;

void write_zlib_header(BitWriter& writer) {
  writer.write_lsb(0x78, 8);  // CMF: deflate, 32 KiB window
  writer.write_lsb(0x01, 8);  // FLG: (0x7801 % 31) == 0, no preset dictionary
}

void write_stored_block_header_and_length(BitWriter& writer, bool bfinal,
                                          std::uint16_t length) {
  writer.write_lsb(bfinal ? 1u : 0u, 1);
  writer.write_lsb(0, 2);  // BTYPE = 00 (stored)
  writer.align_to_byte();
  writer.write_lsb(length, 16);                 // LEN (little-endian bits)
  writer.write_lsb(~length & 0xFFFFu, 16);      // NLEN
}

// --- RFC 1951 §3.2.5 fixed Huffman code lookup ------------------------------

struct HuffmanCode {
  std::uint16_t canonical;
  std::uint8_t length;
};

HuffmanCode fixed_literal_length_code(std::uint16_t symbol) {
  if (symbol <= 143) {
    return HuffmanCode{static_cast<std::uint16_t>(0x30u + symbol), 8};
  }
  if (symbol <= 255) {
    return HuffmanCode{static_cast<std::uint16_t>(0x190u + symbol - 144), 9};
  }
  if (symbol <= 279) {
    return HuffmanCode{static_cast<std::uint16_t>(symbol - 256), 7};
  }
  if (symbol <= 287) {
    return HuffmanCode{static_cast<std::uint16_t>(0xC0u + symbol - 280), 8};
  }
  throw std::invalid_argument("WP-607C literal/length symbol out of range");
}

HuffmanCode fixed_distance_code(std::uint8_t symbol) {
  if (symbol <= 29) {
    return HuffmanCode{symbol, 5};
  }
  throw std::invalid_argument("WP-607C distance symbol out of range");
}

struct LengthEncoding {
  std::uint16_t symbol;
  std::uint8_t extra_bits;
  std::uint16_t extra_value;
};

// RFC 1951 §3.2.5 length 3..258 -> symbol 257..285 plus extra bits.
LengthEncoding encode_length(std::uint16_t length) {
  struct Entry {
    std::uint16_t base;
    std::uint8_t extra;
  };
  static constexpr Entry kTable[24] = {
      {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0},
      {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2},
      {31, 2}, {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4},
      {99, 4}, {115, 4}};
  struct Top {
    std::uint16_t base;
    std::uint8_t extra;
  };
  static constexpr Top kTop[5] = {{131, 4}, {163, 5}, {195, 5}, {227, 5},
                                  {257, 0}};
  if (length < 3 || length > 258) {
    throw std::invalid_argument("WP-607C match length out of range");
  }
  if (length == 258) {
    return LengthEncoding{285, 0, 0};
  }
  for (unsigned i = 0; i < 24; ++i) {
    const auto& entry = kTable[i];
    const std::uint16_t next =
        i + 1 < 24 ? kTable[i + 1].base : kTop[0].base;
    if (length >= entry.base && length < next) {
      return LengthEncoding{static_cast<std::uint16_t>(257 + i), entry.extra,
                            static_cast<std::uint16_t>(length - entry.base)};
    }
  }
  throw std::invalid_argument("WP-607C match length not covered");
}

LengthEncoding encode_distance(std::uint16_t distance) {
  struct Entry {
    std::uint16_t base;
    std::uint8_t extra;
  };
  static constexpr Entry kTable[16] = {
      {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1}, {9, 2}, {13, 2},
      {17, 3}, {25, 3}, {33, 4}, {49, 4}, {65, 5}, {97, 5}, {129, 6},
      {193, 6}};
  struct Top {
    std::uint16_t base;
    std::uint8_t extra;
  };
  static constexpr Top kTop[14] = {
      {257, 6}, {385, 7}, {513, 7}, {769, 8}, {1025, 8}, {1537, 9},
      {2049, 9}, {3073, 10}, {4097, 10}, {6145, 11}, {8193, 11},
      {12289, 12}, {16385, 12}, {24577, 13}};
  if (distance < 1 || distance > 32768) {
    throw std::invalid_argument("WP-607C match distance out of range");
  }
  for (unsigned i = 0; i < 16; ++i) {
    const auto& entry = kTable[i];
    const std::uint16_t next =
        i + 1 < 16 ? kTable[i + 1].base : kTop[0].base;
    if (distance >= entry.base && distance < next) {
      return LengthEncoding{static_cast<std::uint16_t>(i), entry.extra,
                            static_cast<std::uint16_t>(distance - entry.base)};
    }
  }
  for (unsigned i = 0; i < 14; ++i) {
    const auto& entry = kTop[i];
    const std::uint16_t next =
        i + 1 < 14 ? kTop[i + 1].base : 32769;
    if (distance >= entry.base && distance < next) {
      return LengthEncoding{static_cast<std::uint16_t>(16 + i), entry.extra,
                            static_cast<std::uint16_t>(distance - entry.base)};
    }
  }
  throw std::invalid_argument("WP-607C match distance not covered");
}

void write_fixed_literal(BitWriter& writer, std::uint8_t value,
                         ByteRangeFact& input_bits) {
  const HuffmanCode code = fixed_literal_length_code(value);
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(code.canonical, code.length);
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

void write_fixed_match(BitWriter& writer, std::uint16_t length,
                       std::uint16_t distance, ByteRangeFact& input_bits) {
  const LengthEncoding length_code = encode_length(length);
  const LengthEncoding distance_code = encode_distance(distance);
  const HuffmanCode length_symbol = fixed_literal_length_code(
      length_code.symbol);
  const HuffmanCode distance_symbol = fixed_distance_code(
      static_cast<std::uint8_t>(distance_code.symbol));
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(length_symbol.canonical, length_symbol.length);
  if (length_code.extra_bits != 0) {
    writer.write_lsb(length_code.extra_value, length_code.extra_bits);
  }
  writer.write_canonical(distance_symbol.canonical, distance_symbol.length);
  if (distance_code.extra_bits != 0) {
    writer.write_lsb(distance_code.extra_value, distance_code.extra_bits);
  }
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

void write_fixed_end_of_block(BitWriter& writer, ByteRangeFact& input_bits) {
  const HuffmanCode code = fixed_literal_length_code(256);
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(code.canonical, code.length);
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

// --- frozen Dynamic block header --------------------------------------------
//
// The header encodes literal/length code lengths {65:1, 256:2, 260:3, 261:3}
// and distance code lengths {0:1, 1:1} through a complete code-length
// alphabet {0:2, 1:3, 2:3, 3:3, 16:3, 17:3, 18:3}. The instruction sequence
// contains exactly one repeat of each kind, in the order 16, 17, 18:
//   [0 x59] 16(+3) 1 17(+7) 18(+127) [0 x42] 2 [0 x3] 3 3 1 1
// which expands to lengths [0x65, 1, 0x190, 2, 0x3, 3, 3, 1, 1].

constexpr std::uint8_t kHeaderCodeLengthLengths[18] = {
    3, 3, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0, 3};

struct DynamicCode {
  std::uint16_t canonical;
  std::uint8_t length;
};

// Canonical codes of the code-length alphabet above.
DynamicCode header_code_length_code(std::uint8_t symbol) {
  switch (symbol) {
    case 0:
      return {0b00, 2};
    case 1:
      return {0b010, 3};
    case 2:
      return {0b011, 3};
    case 3:
      return {0b100, 3};
    case 16:
      return {0b101, 3};
    case 17:
      return {0b110, 3};
    case 18:
      return {0b111, 3};
    default:
      throw std::invalid_argument("WP-607C symbol not in the code-length code");
  }
}

// Literal/length and distance codes of the frozen dynamic tables.
DynamicCode dynamic_literal_length_code(std::uint16_t symbol) {
  switch (symbol) {
    case 0x41:
      return {0b0, 1};
    case 256:
      return {0b10, 2};
    case 260:
      return {0b110, 3};
    case 261:
      return {0b111, 3};
    default:
      throw std::invalid_argument(
          "WP-607C symbol not in the dynamic literal/length code");
  }
}

DynamicCode dynamic_distance_code(std::uint8_t symbol) {
  switch (symbol) {
    case 0:
      return {0b0, 1};
    case 1:
      return {0b1, 1};
    default:
      throw std::invalid_argument(
          "WP-607C symbol not in the dynamic distance code");
  }
}

void write_dynamic_header(BitWriter& writer, bool bfinal,
                          const std::vector<std::uint8_t>& repeats) {
  if (repeats != std::vector<std::uint8_t>{16, 17, 18}) {
    throw std::invalid_argument(
        "WP-607C frozen dynamic header only encodes repeats 16, 17, 18");
  }
  writer.write_lsb(bfinal ? 1u : 0u, 1);
  writer.write_lsb(2, 2);  // BTYPE = 10 (dynamic)
  writer.write_lsb(5, 5);  // HLIT: 262 literal/length code lengths
  writer.write_lsb(1, 5);  // HDIST: 2 distance code lengths
  writer.write_lsb(14, 4);  // HCLEN: 18 code-length code lengths
  for (const std::uint8_t length : kHeaderCodeLengthLengths) {
    writer.write_lsb(length, 3);
  }
  struct Instruction {
    std::uint8_t symbol;
    std::uint8_t extra_bits;
    std::uint32_t extra_value;
  };
  const auto instruction_run = [&](std::uint8_t symbol, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      const DynamicCode code = header_code_length_code(symbol);
      writer.write_canonical(code.canonical, code.length);
    }
  };
  instruction_run(0, 59);                  // 59 zeros (symbols 0..58)
  {
    const DynamicCode code = header_code_length_code(16);
    writer.write_canonical(code.canonical, code.length);
    writer.write_lsb(3, 2);                // repeat the zero 6 more times
  }
  {
    const DynamicCode code = header_code_length_code(1);
    writer.write_canonical(code.canonical, code.length);  // 65 -> length 1
  }
  {
    const DynamicCode code = header_code_length_code(17);
    writer.write_canonical(code.canonical, code.length);
    writer.write_lsb(7, 3);                // 10 zeros (66..75)
  }
  {
    const DynamicCode code = header_code_length_code(18);
    writer.write_canonical(code.canonical, code.length);
    writer.write_lsb(127, 7);              // 138 zeros (76..213)
  }
  instruction_run(0, 42);                  // 42 zeros (214..255)
  {
    const DynamicCode code = header_code_length_code(2);
    writer.write_canonical(code.canonical, code.length);  // 256 -> length 2
  }
  instruction_run(0, 3);                   // zeros (257..259)
  {
    const DynamicCode code = header_code_length_code(3);
    writer.write_canonical(code.canonical, code.length);  // 260 -> length 3
    writer.write_canonical(code.canonical, code.length);  // 261 -> length 3
  }
  {
    const DynamicCode code = header_code_length_code(1);
    writer.write_canonical(code.canonical, code.length);  // distance 0
    writer.write_canonical(code.canonical, code.length);  // distance 1
  }
}

void write_dynamic_literal(BitWriter& writer, std::uint8_t value,
                           ByteRangeFact& input_bits) {
  const DynamicCode code = dynamic_literal_length_code(value);
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(code.canonical, code.length);
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

void write_dynamic_match(BitWriter& writer, std::uint16_t length,
                         std::uint16_t distance, ByteRangeFact& input_bits) {
  const LengthEncoding length_code = encode_length(length);
  const LengthEncoding distance_code = encode_distance(distance);
  const DynamicCode length_symbol =
      dynamic_literal_length_code(length_code.symbol);
  const DynamicCode distance_symbol =
      dynamic_distance_code(static_cast<std::uint8_t>(distance_code.symbol));
  if (length_code.extra_bits != 0 || distance_code.extra_bits != 0) {
    throw std::invalid_argument(
        "WP-607C frozen dynamic match uses extra bits");
  }
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(length_symbol.canonical, length_symbol.length);
  writer.write_canonical(distance_symbol.canonical, distance_symbol.length);
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

void write_dynamic_end_of_block(BitWriter& writer, ByteRangeFact& input_bits) {
  const DynamicCode code = dynamic_literal_length_code(256);
  input_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_canonical(code.canonical, code.length);
  input_bits.end = writer.bit_position() - kZlibHeaderBits;
}

// --- shared completion of a bit-written trace case ---------------------------

// Trace-case IHDR geometry (gray8): stored/fixed/multiblock wrap six raw
// bytes as two 2x2 scanlines; the dynamic case freezes the plan's 8x0x41
// raw output, declared as one 3x2 stream of matching length.
struct TraceIhdr {
  std::uint32_t width;
  std::uint32_t height;
};

TraceIhdr trace_ihdr(ControlledCaseId id) {
  switch (id) {
    case ControlledCaseId::kTraceDynamicOverlapRepeats:
      return {3, 2};
    default:
      return {2, 2};
  }
}

TokenFact make_literal_fact(TokenKind kind, ByteRangeFact input_bits,
                            std::uint64_t output_begin, std::uint64_t output_end,
                            std::optional<std::uint8_t> literal) {
  TokenFact token;
  token.kind = kind;
  token.input_bits = input_bits;
  token.output_bytes = ByteRangeFact{output_begin, output_end};
  token.literal = literal;
  return token;
}

TokenFact make_match_fact(ByteRangeFact input_bits, std::uint64_t output_begin,
                          std::uint64_t output_end, std::uint16_t length,
                          std::uint16_t distance, ByteRangeFact match_source) {
  TokenFact token;
  token.kind = TokenKind::kMatch;
  token.input_bits = input_bits;
  token.output_bytes = ByteRangeFact{output_begin, output_end};
  token.length = length;
  token.distance = distance;
  token.match_source = match_source;
  return token;
}

TokenFact make_end_of_block_fact(ByteRangeFact input_bits,
                                 std::uint64_t output_cursor) {
  TokenFact token;
  token.kind = TokenKind::kEndOfBlock;
  token.input_bits = input_bits;
  token.output_bytes = ByteRangeFact{output_cursor, output_cursor};
  return token;
}

std::uint32_t raw_adler32(const std::vector<std::byte>& raw) {
  if (raw.size() > std::numeric_limits<uInt>::max()) {
    throw std::invalid_argument("WP-607C raw output exceeds the zlib API limit");
  }
  return static_cast<std::uint32_t>(adler32(
      adler32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(raw.data()),
      static_cast<uInt>(raw.size())));
}

ControlledFixture finish_png_case(ControlledCaseId id, BitWriter& writer,
                                  std::vector<std::byte> raw_output) {
  writer.align_to_byte();
  std::vector<std::byte> stream(writer.bytes().begin(), writer.bytes().end());
  append_u32_be(stream, raw_adler32(raw_output));

  std::vector<std::byte> png;
  const TraceIhdr dims = trace_ihdr(id);
  begin_png(png, dims.width, dims.height, 8, 0, 0);
  ControlledFixture fixture;
  fixture.id = id;
  fixture.stable_id = "trace";  // replaced by the caller
  fixture.png_bytes = finish_png(std::move(png), std::move(stream));
  return fixture;
}

// --- the four valid trace case builders --------------------------------------

ControlledFixture make_trace_stored_literals() {
  const std::vector<std::byte> raw = {B(0x00), B(0x41), B(0x42),
                                      B(0x00), B(0x43), B(0x44)};
  BitWriter writer;
  write_zlib_header(writer);
  ByteRangeFact block_bits{};
  block_bits.begin = writer.bit_position() - kZlibHeaderBits;
  write_stored_block_header_and_length(writer, true,
                                       static_cast<std::uint16_t>(raw.size()));
  std::vector<TokenFact> tokens;
  std::uint64_t output_cursor = 0;
  for (const std::byte literal : raw) {
    ByteRangeFact input{};
    input.begin = writer.bit_position() - kZlibHeaderBits;
    writer.write_lsb(U(literal), 8);
    input.end = writer.bit_position() - kZlibHeaderBits;
    tokens.push_back(make_literal_fact(TokenKind::kLiteral, input,
                                       output_cursor, output_cursor + 1,
                                       U(literal)));
    ++output_cursor;
  }
  block_bits.end = writer.bit_position() - kZlibHeaderBits;
  tokens.push_back(make_end_of_block_fact(
      ByteRangeFact{block_bits.end, block_bits.end}, output_cursor));

  ControlledFixture fixture =
      finish_png_case(ControlledCaseId::kTraceStoredLiterals, writer, raw);
  fixture.stable_id = "trace-stored-literals";
  BlockFact block;
  block.kind = BlockKind::kStored;
  block.bfinal = true;
  block.input_bits = block_bits;
  block.output_bytes = ByteRangeFact{0, output_cursor};
  fixture.expected.blocks.push_back(std::move(block));
  fixture.expected.tokens = std::move(tokens);
  return fixture;
}

ControlledFixture make_trace_fixed_nonoverlap() {
  // Literals A, B, C then a length-3 distance-3 match: the source [0,3) never
  // overlaps the target [3,6). Raw output mirrors a 2x2 gray8 frame whose two
  // rows are identical.
  const std::vector<std::uint8_t> literals = {0x00, 0x41, 0x42};
  const std::uint16_t kLength = 3;
  const std::uint16_t kDistance = 3;
  const std::vector<std::byte> raw = {B(0x00), B(0x41), B(0x42),
                                      B(0x00), B(0x41), B(0x42)};

  BitWriter writer;
  write_zlib_header(writer);
  ByteRangeFact block_bits{};
  block_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_lsb(1, 1);  // BFINAL
  writer.write_lsb(1, 2);  // BTYPE = 01 (fixed)
  std::vector<TokenFact> tokens;
  std::uint64_t output_cursor = 0;
  for (const std::uint8_t literal : literals) {
    ByteRangeFact input{};
    write_fixed_literal(writer, literal, input);
    tokens.push_back(make_literal_fact(TokenKind::kLiteral, input,
                                       output_cursor, output_cursor + 1,
                                       literal));
    ++output_cursor;
  }
  ByteRangeFact match_input{};
  write_fixed_match(writer, kLength, kDistance, match_input);
  tokens.push_back(make_match_fact(match_input, output_cursor,
                                   output_cursor + kLength, kLength, kDistance,
                                   ByteRangeFact{0, static_cast<std::uint64_t>(
                                                        literals.size())}));
  output_cursor += kLength;
  ByteRangeFact eob_input{};
  write_fixed_end_of_block(writer, eob_input);
  tokens.push_back(
      make_end_of_block_fact(eob_input, output_cursor));
  block_bits.end = writer.bit_position() - kZlibHeaderBits;

  ControlledFixture fixture =
      finish_png_case(ControlledCaseId::kTraceFixedNonoverlap, writer, raw);
  fixture.stable_id = "trace-fixed-nonoverlap";
  BlockFact block;
  block.kind = BlockKind::kFixed;
  block.bfinal = true;
  block.input_bits = block_bits;
  block.output_bytes = ByteRangeFact{0, output_cursor};
  fixture.expected.blocks.push_back(std::move(block));
  fixture.expected.tokens = std::move(tokens);
  return fixture;
}

ControlledFixture make_trace_dynamic_overlap_repeats() {
  // Literal A, literal A, then a length-6 distance-1 overlapping match, so
  // the match is expected token index 2 and the output is eight 0x41 bytes.
  const std::uint16_t kLength = 6;
  const std::uint16_t kDistance = 1;
  BitWriter writer;
  write_zlib_header(writer);
  ByteRangeFact block_bits{};
  block_bits.begin = writer.bit_position() - kZlibHeaderBits;
  write_dynamic_header(writer, true, {16, 17, 18});
  std::vector<TokenFact> tokens;
  ByteRangeFact first_input{};
  write_dynamic_literal(writer, 0x41, first_input);
  tokens.push_back(make_literal_fact(TokenKind::kLiteral, first_input, 0, 1,
                                     0x41));
  ByteRangeFact second_input{};
  write_dynamic_literal(writer, 0x41, second_input);
  tokens.push_back(make_literal_fact(TokenKind::kLiteral, second_input, 1, 2,
                                     0x41));
  ByteRangeFact match_input{};
  write_dynamic_match(writer, kLength, kDistance, match_input);
  tokens.push_back(make_match_fact(match_input, 2, 2 + kLength, kLength,
                                   kDistance,
                                   ByteRangeFact{1, 2}));
  ByteRangeFact eob_input{};
  write_dynamic_end_of_block(writer, eob_input);
  tokens.push_back(make_end_of_block_fact(eob_input, 2 + kLength));
  block_bits.end = writer.bit_position() - kZlibHeaderBits;

  ControlledFixture fixture = finish_png_case(
      ControlledCaseId::kTraceDynamicOverlapRepeats, writer,
      std::vector<std::byte>(8, B(0x41)));
  fixture.stable_id = "trace-dynamic-overlap-repeats";
  BlockFact block;
  block.kind = BlockKind::kDynamic;
  block.bfinal = true;
  block.input_bits = block_bits;
  block.output_bytes = ByteRangeFact{0, 2 + kLength};
  fixture.expected.blocks.push_back(std::move(block));
  fixture.expected.tokens = std::move(tokens);
  fixture.expected.expected_code_length_repeats = {16, 17, 18};
  return fixture;
}

ControlledFixture make_trace_multiblock_bfinal() {
  // Exact block order Stored, Fixed, Dynamic with BFINAL false, false, true.
  // The raw stream is a valid 2x2 gray8 filtered frame: [00 41 42][00 41 41].
  BitWriter writer;
  write_zlib_header(writer);
  std::vector<BlockFact> blocks;
  std::vector<TokenFact> tokens;
  std::uint64_t output_cursor = 0;

  // Block 0: stored "00 41".
  ByteRangeFact stored_bits{};
  stored_bits.begin = writer.bit_position() - kZlibHeaderBits;
  write_stored_block_header_and_length(writer, false, 2);
  for (const std::uint8_t literal : {0x00u, 0x41u}) {
    ByteRangeFact input{};
    input.begin = writer.bit_position() - kZlibHeaderBits;
    writer.write_lsb(literal, 8);
    input.end = writer.bit_position() - kZlibHeaderBits;
    tokens.push_back(make_literal_fact(TokenKind::kLiteral, input,
                                       output_cursor, output_cursor + 1,
                                       literal));
    ++output_cursor;
  }
  stored_bits.end = writer.bit_position() - kZlibHeaderBits;
  tokens.push_back(make_end_of_block_fact(
      ByteRangeFact{stored_bits.end, stored_bits.end}, output_cursor));
  BlockFact stored_block;
  stored_block.kind = BlockKind::kStored;
  stored_block.bfinal = false;
  stored_block.input_bits = stored_bits;
  stored_block.output_bytes = ByteRangeFact{0, output_cursor};
  blocks.push_back(std::move(stored_block));

  // Block 1: fixed literals 42 00.
  ByteRangeFact fixed_bits{};
  fixed_bits.begin = writer.bit_position() - kZlibHeaderBits;
  writer.write_lsb(0, 1);  // BFINAL
  writer.write_lsb(1, 2);  // BTYPE = 01 (fixed)
  for (const std::uint8_t literal : {0x42u, 0x00u}) {
    ByteRangeFact input{};
    write_fixed_literal(writer, literal, input);
    tokens.push_back(make_literal_fact(TokenKind::kLiteral, input,
                                       output_cursor, output_cursor + 1,
                                       literal));
    ++output_cursor;
  }
  ByteRangeFact eob_input{};
  write_fixed_end_of_block(writer, eob_input);
  tokens.push_back(make_end_of_block_fact(eob_input, output_cursor));
  fixed_bits.end = writer.bit_position() - kZlibHeaderBits;
  BlockFact fixed_block;
  fixed_block.kind = BlockKind::kFixed;
  fixed_block.bfinal = false;
  fixed_block.input_bits = fixed_bits;
  fixed_block.output_bytes = ByteRangeFact{2, output_cursor};
  blocks.push_back(std::move(fixed_block));

  // Block 2: dynamic literals 41 41 (reuses the frozen dynamic header).
  ByteRangeFact dynamic_bits{};
  dynamic_bits.begin = writer.bit_position() - kZlibHeaderBits;
  write_dynamic_header(writer, true, {16, 17, 18});
  for (unsigned i = 0; i < 2; ++i) {
    ByteRangeFact input{};
    write_dynamic_literal(writer, 0x41, input);
    tokens.push_back(make_literal_fact(TokenKind::kLiteral, input,
                                       output_cursor, output_cursor + 1,
                                       0x41));
    ++output_cursor;
  }
  ByteRangeFact dynamic_eob_input{};
  write_dynamic_end_of_block(writer, dynamic_eob_input);
  tokens.push_back(make_end_of_block_fact(dynamic_eob_input, output_cursor));
  dynamic_bits.end = writer.bit_position() - kZlibHeaderBits;
  BlockFact dynamic_block;
  dynamic_block.kind = BlockKind::kDynamic;
  dynamic_block.bfinal = true;
  dynamic_block.input_bits = dynamic_bits;
  dynamic_block.output_bytes = ByteRangeFact{4, output_cursor};
  blocks.push_back(std::move(dynamic_block));

  const std::vector<std::byte> raw = {B(0x00), B(0x41), B(0x42),
                                      B(0x00), B(0x41), B(0x41)};
  ControlledFixture fixture = finish_png_case(
      ControlledCaseId::kTraceMultiblockBfinal, writer, raw);
  fixture.stable_id = "trace-multiblock-bfinal";
  fixture.expected.blocks = std::move(blocks);
  fixture.expected.tokens = std::move(tokens);
  fixture.expected.expected_code_length_repeats = {16, 17, 18};
  return fixture;
}

// --- test-side chunk scanning over fixture bytes (checked) -------------------

struct ScannedChunk {
  std::string type;
  std::uint64_t data_offset = 0;
  std::uint64_t data_length = 0;
  std::uint64_t crc_offset = 0;
};

std::vector<ScannedChunk> scan_png_chunks(const std::vector<std::byte>& png) {
  constexpr std::uint64_t kSignatureSize = 8;
  if (png.size() < kSignatureSize) {
    throw std::invalid_argument("WP-607C fixture signature is truncated");
  }
  std::vector<ScannedChunk> chunks;
  std::uint64_t pos = kSignatureSize;
  while (pos + 8 <= png.size()) {
    ScannedChunk chunk;
    std::uint64_t length = 0;
    for (unsigned i = 0; i < 4; ++i) {
      length = (length << 8) | U(png[static_cast<std::size_t>(pos + i)]);
    }
    chunk.data_offset = pos + 8;
    if (length > png.size() - chunk.data_offset) {
      throw std::invalid_argument("WP-607C fixture chunk data is truncated");
    }
    chunk.data_length = length;
    chunk.crc_offset = chunk.data_offset + length;
    if (chunk.crc_offset + 4 > png.size()) {
      throw std::invalid_argument("WP-607C fixture chunk CRC is truncated");
    }
    for (unsigned i = 0; i < 4; ++i) {
      chunk.type.push_back(
          static_cast<char>(U(png[static_cast<std::size_t>(pos + 4 + i)])));
    }
    chunks.push_back(std::move(chunk));
    pos = chunk.crc_offset + 4;
  }
  if (pos != png.size()) {
    throw std::invalid_argument("WP-607C fixture chunk framing is invalid");
  }
  return chunks;
}

const ScannedChunk& find_single_idat(const std::vector<ScannedChunk>& chunks) {
  const ScannedChunk* idat = nullptr;
  for (const auto& chunk : chunks) {
    if (chunk.type == "IDAT") {
      if (idat != nullptr) {
        throw std::invalid_argument(
            "WP-607C mutation expects a single IDAT chunk");
      }
      idat = &chunk;
    }
  }
  if (idat == nullptr) {
    throw std::invalid_argument("WP-607C fixture has no IDAT chunk");
  }
  return *idat;
}

std::vector<std::byte> single_idat_payload(const std::vector<std::byte>& png) {
  const auto chunks = scan_png_chunks(png);
  const ScannedChunk& idat = find_single_idat(chunks);
  return std::vector<std::byte>(
      png.begin() + static_cast<std::ptrdiff_t>(idat.data_offset),
      png.begin() + static_cast<std::ptrdiff_t>(idat.data_offset +
                                                idat.data_length));
}

// Rebuilds the fixture PNG with `payload` as its single IDAT payload; every
// chunk CRC is recomputed by push_chunk.
std::vector<std::byte> rebuild_png_with_idat(
    const std::vector<std::byte>& png, const std::vector<std::byte>& payload) {
  const auto chunks = scan_png_chunks(png);
  std::vector<std::byte> out(png.begin(), png.begin() + 8);
  bool replaced = false;
  for (const auto& chunk : chunks) {
    if (chunk.type == "IDAT") {
      push_chunk(out, "IDAT", payload);
      replaced = true;
      continue;
    }
    push_chunk(out, chunk.type,
               std::span<const std::byte>(
                   png.data() + chunk.data_offset,
                   static_cast<std::size_t>(chunk.data_length)));
  }
  if (!replaced) {
    throw std::invalid_argument("WP-607C fixture has no IDAT chunk");
  }
  return out;
}

// Test-side logical-to-physical mapping over fixture bytes (mirrors the
// production VirtualIDATStream contract for the ranges the corpus freezes).
std::vector<ByteRangeFact> map_logical_to_physical(
    const std::vector<std::byte>& png, std::uint64_t logical_offset,
    std::uint64_t length) {
  std::size_t range_end = 0;
  if (!checked_append_size(static_cast<std::size_t>(logical_offset),
                           static_cast<std::size_t>(length), range_end)) {
    throw std::invalid_argument("WP-607C logical range overflows");
  }
  const auto chunks = scan_png_chunks(png);
  std::vector<ByteRangeFact> spans;
  std::size_t logical = 0;
  for (const auto& chunk : chunks) {
    if (chunk.type != "IDAT") {
      continue;
    }
    std::size_t logical_end = 0;
    if (!checked_append_size(logical,
                             static_cast<std::size_t>(chunk.data_length),
                             logical_end)) {
      throw std::invalid_argument("WP-607C logical stream size overflows");
    }
    const std::uint64_t overlap_begin = std::max<std::uint64_t>(
        logical, logical_offset);
    const std::uint64_t overlap_end =
        std::min<std::uint64_t>(logical_end, range_end);
    if (overlap_begin < overlap_end) {
      spans.push_back(ByteRangeFact{
          chunk.data_offset + (overlap_begin - logical),
          chunk.data_offset + (overlap_end - logical)});
    }
    logical = logical_end;
  }
  return spans;
}

// --- split and mutation builders ---------------------------------------------

ControlledFixture split_idat(ControlledFixture base,
                             std::span<const std::uint64_t> logical_splits) {
  const std::vector<std::byte> payload = single_idat_payload(base.png_bytes);
  std::vector<std::uint64_t> splits(logical_splits.begin(),
                                    logical_splits.end());
  std::sort(splits.begin(), splits.end());
  splits.erase(std::unique(splits.begin(), splits.end()), splits.end());
  if (splits.empty() || splits.front() == 0 ||
      splits.back() >= payload.size()) {
    throw std::invalid_argument("WP-607C IDAT split is out of bounds");
  }
  std::vector<std::uint64_t> bounds = splits;
  bounds.push_back(payload.size());

  const auto chunks = scan_png_chunks(base.png_bytes);
  find_single_idat(chunks);  // the splitter requires a single IDAT
  std::vector<std::byte> out(base.png_bytes.begin(), base.png_bytes.begin() + 8);
  bool emitted = false;
  for (const auto& chunk : chunks) {
    if (chunk.type != "IDAT") {
      push_chunk(out, chunk.type,
                 std::span<const std::byte>(
                     base.png_bytes.data() + chunk.data_offset,
                     static_cast<std::size_t>(chunk.data_length)));
      continue;
    }
    // Replace the original payload with the ordered split pieces; every piece
    // CRC is recomputed by push_chunk.
    std::uint64_t piece_begin = 0;
    for (const std::uint64_t bound : bounds) {
      push_chunk(out, "IDAT",
                 std::span<const std::byte>(
                     payload.data() + piece_begin,
                     static_cast<std::size_t>(bound - piece_begin)));
      piece_begin = bound;
    }
    emitted = true;
  }
  if (!emitted) {
    throw std::invalid_argument("WP-607C fixture has no IDAT chunk");
  }
  base.png_bytes = std::move(out);
  return base;
}

ControlledFixture truncate_deflate_header(ControlledFixture base,
                                          std::uint64_t keep_bits) {
  const std::vector<std::byte> payload = single_idat_payload(base.png_bytes);
  if (keep_bits == 0 || keep_bits > 56) {
    throw std::invalid_argument("WP-607C truncation is out of bounds");
  }
  // IDAT payloads are byte-granular, so a partially kept byte is dropped: the
  // cut is exact at the bit level and the stream physically ends before any
  // partially kept byte's bits.
  const std::uint64_t keep_bytes = keep_bits / 8;
  if (payload.size() < 2 + keep_bytes + 4) {
    throw std::invalid_argument("WP-607C payload is too short to truncate");
  }
  std::vector<std::byte> cut(payload.begin(),
                             payload.begin() + static_cast<std::ptrdiff_t>(
                                                   2 + keep_bytes));
  // The untouched trailer keeps the original Adler-32 bytes so the named
  // fault stays the truncated block header alone.
  cut.insert(cut.end(), payload.end() - 4, payload.end());
  base.png_bytes = rebuild_png_with_idat(base.png_bytes, cut);
  return base;
}

ControlledFixture truncate_huffman_token(ControlledFixture base,
                                         std::uint64_t keep_bits) {
  return truncate_deflate_header(std::move(base), keep_bits);
}

ControlledFixture make_reserved_btype() {
  ControlledFixture base = make_trace_stored_literals();
  std::vector<std::byte> payload = single_idat_payload(base.png_bytes);
  payload[2] = B(0x06);  // BFINAL=0, BTYPE=11 (the rest is padding)
  base.png_bytes = rebuild_png_with_idat(base.png_bytes, payload);
  return base;
}

ControlledFixture make_invalid_distance() {
  // Literal A (one output byte) followed by length 3 at distance 2: the match
  // reaches two bytes back where only one byte exists.
  BitWriter writer;
  write_zlib_header(writer);
  writer.write_lsb(1, 1);  // BFINAL
  writer.write_lsb(1, 2);  // BTYPE = 01 (fixed)
  ByteRangeFact unused_range{};
  write_fixed_literal(writer, 0x41, unused_range);
  const LengthEncoding length_code = encode_length(3);
  const HuffmanCode length_symbol =
      fixed_literal_length_code(length_code.symbol);
  writer.write_canonical(length_symbol.canonical, length_symbol.length);
  const LengthEncoding distance_code = encode_distance(2);
  const HuffmanCode distance_symbol =
      fixed_distance_code(static_cast<std::uint8_t>(distance_code.symbol));
  writer.write_canonical(distance_symbol.canonical, distance_symbol.length);
  write_fixed_end_of_block(writer, unused_range);
  writer.align_to_byte();
  const std::vector<std::byte> raw = {B(0x41)};
  std::vector<std::byte> stream(writer.bytes().begin(), writer.bytes().end());
  append_u32_be(stream, raw_adler32(raw));

  std::vector<std::byte> png;
  begin_png(png, 1, 1, 8, 0, 0);
  ControlledFixture fixture;
  fixture.id = ControlledCaseId::kErrorInvalidDistance;
  fixture.stable_id = "error-invalid-distance";
  fixture.png_bytes = finish_png(std::move(png), std::move(stream));
  return fixture;
}

ControlledFixture corrupt_idat_crc(ControlledFixture base) {
  const auto chunks = scan_png_chunks(base.png_bytes);
  const ScannedChunk& idat = find_single_idat(chunks);
  base.png_bytes[static_cast<std::size_t>(idat.crc_offset)] =
      base.png_bytes[static_cast<std::size_t>(idat.crc_offset)] ^
      std::byte{0x01};
  return base;
}

ControlledFixture corrupt_adler_and_repair_crc(ControlledFixture base) {
  std::vector<std::byte> payload = single_idat_payload(base.png_bytes);
  payload[payload.size() - 4] =
      payload[payload.size() - 4] ^ std::byte{0x01};
  base.png_bytes = rebuild_png_with_idat(base.png_bytes, payload);
  return base;
}

// --- the nine split and malformed case builders -------------------------------

ControlledFixture make_idat_split_zlib_header() {
  const std::vector<std::uint64_t> kSplitAt = {1};
  ControlledFixture fixture = split_idat(make_trace_stored_literals(), kSplitAt);
  fixture.id = ControlledCaseId::kIdatSplitZlibHeader;
  fixture.stable_id = "idat-split-zlib-header";
  fixture.expected.physical_spans =
      map_logical_to_physical(fixture.png_bytes, 0, 2);
  return fixture;
}

ControlledFixture make_idat_split_token() {
  // Token 0 ('A') covers logical bits [19,27) -> bytes [2,4); the split at
  // byte 3 lands inside the token.
  const std::vector<std::uint64_t> kSplitAt = {3};
  ControlledFixture fixture = split_idat(make_trace_fixed_nonoverlap(), kSplitAt);
  fixture.id = ControlledCaseId::kIdatSplitToken;
  fixture.stable_id = "idat-split-token";
  fixture.expected.physical_spans =
      map_logical_to_physical(fixture.png_bytes, 2, 2);
  return fixture;
}

ControlledFixture make_idat_split_adler() {
  // The Adler trailer occupies the last four logical bytes [13,17); the split
  // at byte 15 divides it into two spans of two bytes.
  const std::vector<std::uint64_t> kSplitAt = {15};
  ControlledFixture fixture = split_idat(make_trace_stored_literals(), kSplitAt);
  fixture.id = ControlledCaseId::kIdatSplitAdler;
  fixture.stable_id = "idat-split-adler";
  fixture.expected.physical_spans =
      map_logical_to_physical(fixture.png_bytes, 13, 4);
  return fixture;
}

ControlledFixture make_error_truncated_header() {
  ControlledFixture fixture =
      truncate_deflate_header(make_trace_stored_literals(), 2);
  fixture.id = ControlledCaseId::kErrorTruncatedHeader;
  fixture.stable_id = "error-truncated-header";
  ErrorFacts error;
  error.decoder_message = "truncated block header";
  error.stop_input_bit = 16;
  error.stop_output_byte = 0;
  fixture.expected.error = std::move(error);
  return fixture;
}

ControlledFixture make_error_truncated_token() {
  // The first Fixed literal code spans deflate bits [3,11); keeping ten
  // deflate bits cuts the code before its final bit.
  ControlledFixture fixture =
      truncate_huffman_token(make_trace_fixed_nonoverlap(), 10);
  fixture.id = ControlledCaseId::kErrorTruncatedToken;
  fixture.stable_id = "error-truncated-token";
  ErrorFacts error;
  error.decoder_message = "truncated huffman code";
  // The scan fails before verifying anything past the zlib header boundary
  // (the untouched trailer bytes are consumed as deflate input first).
  error.stop_input_bit = 16;
  error.stop_output_byte = 0;
  fixture.expected.error = std::move(error);
  return fixture;
}

ControlledFixture make_error_reserved_btype() {
  ControlledFixture fixture = make_reserved_btype();
  fixture.id = ControlledCaseId::kErrorReservedBtype;
  fixture.stable_id = "error-reserved-btype";
  ErrorFacts error;
  error.decoder_message = "reserved deflate block type";
  error.stop_input_bit = 19;  // zlib header (16) + three header bits
  error.stop_output_byte = 0;
  fixture.expected.error = std::move(error);
  return fixture;
}

ControlledFixture make_error_invalid_distance() {
  ControlledFixture fixture = make_invalid_distance();
  ErrorFacts error;
  error.decoder_message = "distance beyond available output";
  error.stop_input_bit = 16;  // last verified boundary: the zlib header
  error.stop_output_byte = 0;
  fixture.expected.error = std::move(error);
  return fixture;
}

ControlledFixture make_error_crc_mismatch() {
  ControlledFixture fixture = corrupt_idat_crc(make_trace_stored_literals());
  fixture.id = ControlledCaseId::kErrorCrcMismatch;
  fixture.stable_id = "error-crc-mismatch";
  ErrorFacts error;
  error.validation_rule_id = "chunk_crc_mismatch";
  fixture.expected.error = std::move(error);
  return fixture;
}

ControlledFixture make_error_adler_mismatch() {
  ControlledFixture fixture =
      corrupt_adler_and_repair_crc(make_trace_stored_literals());
  fixture.id = ControlledCaseId::kErrorAdlerMismatch;
  fixture.stable_id = "error-adler-mismatch";
  ErrorFacts error;
  error.validation_rule_id = "idat_adler_mismatch";
  fixture.expected.error = std::move(error);
  return fixture;
}

// --- registry ----------------------------------------------------------------

struct CaseInfo {
  ControlledCaseId id;
  std::string_view stable_id;
};

constexpr CaseInfo kCaseInfos[19] = {
    {ControlledCaseId::kUiGray1None, "ui-gray1-none"},
    {ControlledCaseId::kUiIndexed4Trns, "ui-indexed4-trns"},
    {ControlledCaseId::kUiRgb8FiveFilters, "ui-rgb8-five-filters"},
    {ControlledCaseId::kUiRgba16ByteSelect, "ui-rgba16-byte-select"},
    {ControlledCaseId::kUiAdam7EmptyPasses, "ui-adam7-empty-passes"},
    {ControlledCaseId::kTraceStoredLiterals, "trace-stored-literals"},
    {ControlledCaseId::kTraceFixedNonoverlap, "trace-fixed-nonoverlap"},
    {ControlledCaseId::kTraceDynamicOverlapRepeats,
     "trace-dynamic-overlap-repeats"},
    {ControlledCaseId::kTraceMultiblockBfinal, "trace-multiblock-bfinal"},
    {ControlledCaseId::kIdatSplitZlibHeader, "idat-split-zlib-header"},
    {ControlledCaseId::kIdatSplitToken, "idat-split-token"},
    {ControlledCaseId::kIdatSplitAdler, "idat-split-adler"},
    {ControlledCaseId::kErrorTruncatedHeader, "error-truncated-header"},
    {ControlledCaseId::kErrorTruncatedToken, "error-truncated-token"},
    {ControlledCaseId::kErrorReservedBtype, "error-reserved-btype"},
    {ControlledCaseId::kErrorInvalidDistance, "error-invalid-distance"},
    {ControlledCaseId::kErrorCrcMismatch, "error-crc-mismatch"},
    {ControlledCaseId::kErrorAdlerMismatch, "error-adler-mismatch"},
    {ControlledCaseId::kPerfLargeRgba8, "perf-large-rgba8"},
};

}  // namespace

std::span<const ControlledCaseId> all_controlled_cases() noexcept {
  static const ControlledCaseId kAllCases[] = {
      ControlledCaseId::kUiGray1None,
      ControlledCaseId::kUiIndexed4Trns,
      ControlledCaseId::kUiRgb8FiveFilters,
      ControlledCaseId::kUiRgba16ByteSelect,
      ControlledCaseId::kUiAdam7EmptyPasses,
      ControlledCaseId::kTraceStoredLiterals,
      ControlledCaseId::kTraceFixedNonoverlap,
      ControlledCaseId::kTraceDynamicOverlapRepeats,
      ControlledCaseId::kTraceMultiblockBfinal,
      ControlledCaseId::kIdatSplitZlibHeader,
      ControlledCaseId::kIdatSplitToken,
      ControlledCaseId::kIdatSplitAdler,
      ControlledCaseId::kErrorTruncatedHeader,
      ControlledCaseId::kErrorTruncatedToken,
      ControlledCaseId::kErrorReservedBtype,
      ControlledCaseId::kErrorInvalidDistance,
      ControlledCaseId::kErrorCrcMismatch,
      ControlledCaseId::kErrorAdlerMismatch,
      ControlledCaseId::kPerfLargeRgba8,
  };
  return std::span<const ControlledCaseId>(kAllCases);
}

ControlledFixture make_controlled_fixture(ControlledCaseId id) {
  switch (id) {
    case ControlledCaseId::kUiGray1None:
      return make_ui_gray1_none();
    case ControlledCaseId::kUiIndexed4Trns:
      return make_ui_indexed4_trns();
    case ControlledCaseId::kUiRgb8FiveFilters:
      return make_ui_rgb8_five_filters();
    case ControlledCaseId::kUiRgba16ByteSelect:
      return make_ui_rgba16_byte_select();
    case ControlledCaseId::kUiAdam7EmptyPasses:
      return make_ui_adam7_empty_passes();
    case ControlledCaseId::kTraceStoredLiterals:
      return make_trace_stored_literals();
    case ControlledCaseId::kTraceFixedNonoverlap:
      return make_trace_fixed_nonoverlap();
    case ControlledCaseId::kTraceDynamicOverlapRepeats:
      return make_trace_dynamic_overlap_repeats();
    case ControlledCaseId::kTraceMultiblockBfinal:
      return make_trace_multiblock_bfinal();
    case ControlledCaseId::kIdatSplitZlibHeader:
      return make_idat_split_zlib_header();
    case ControlledCaseId::kIdatSplitToken:
      return make_idat_split_token();
    case ControlledCaseId::kIdatSplitAdler:
      return make_idat_split_adler();
    case ControlledCaseId::kErrorTruncatedHeader:
      return make_error_truncated_header();
    case ControlledCaseId::kErrorTruncatedToken:
      return make_error_truncated_token();
    case ControlledCaseId::kErrorReservedBtype:
      return make_error_reserved_btype();
    case ControlledCaseId::kErrorInvalidDistance:
      return make_error_invalid_distance();
    case ControlledCaseId::kErrorCrcMismatch:
      return make_error_crc_mismatch();
    case ControlledCaseId::kErrorAdlerMismatch:
      return make_error_adler_mismatch();
    default:
      throw std::invalid_argument("WP-607C case is not implemented");
  }
}

std::optional<ControlledCaseId> controlled_case_id(std::string_view stable_id) {
  for (const auto& info : kCaseInfos) {
    if (info.stable_id == stable_id) {
      return info.id;
    }
  }
  return std::nullopt;
}

}  // namespace pnga_test::wp607c
