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
