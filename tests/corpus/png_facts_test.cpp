// WP-607C pixel facts: the five valid image cases expose fixed image facts,
// well-formed chunk streams with valid CRCs, exact filtered rows and
// independently declared delivered pixels, cross-checked against a libpng
// oracle (libpng is linked here only as the explicit oracle, never as the
// Trace Backend).

#include "controlled_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <png.h>

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using pnga_test::wp607c::ControlledCaseId;
using pnga_test::wp607c::ControlledFixture;
using pnga_test::wp607c::make_controlled_fixture;

std::byte B(unsigned int value) { return static_cast<std::byte>(value); }

std::uint8_t U(std::byte value) { return std::to_integer<std::uint8_t>(value); }

struct TestChunk {
  std::string type;
  std::vector<std::byte> data;
  std::uint32_t stored_crc = 0;
};

std::uint32_t read_u32_be(const std::vector<std::byte>& bytes,
                          std::size_t offset) {
  return (static_cast<std::uint32_t>(U(bytes[offset])) << 24) |
         (static_cast<std::uint32_t>(U(bytes[offset + 1])) << 16) |
         (static_cast<std::uint32_t>(U(bytes[offset + 2])) << 8) |
         static_cast<std::uint32_t>(U(bytes[offset + 3]));
}

std::vector<TestChunk> parse_chunks(const std::vector<std::byte>& png) {
  const std::vector<std::byte> signature = {
      B(0x89), B('P'), B('N'), B('G'), B(0x0D), B(0x0A), B(0x1A), B(0x0A)};
  REQUIRE(png.size() >= 8);
  REQUIRE(std::vector<std::byte>(png.begin(), png.begin() + 8) == signature);

  std::vector<TestChunk> chunks;
  std::uint64_t pos = 8;
  while (pos + 8 <= png.size()) {
    TestChunk chunk;
    const auto length = read_u32_be(png, static_cast<std::size_t>(pos));
    for (unsigned i = 0; i < 4; ++i) {
      chunk.type.push_back(static_cast<char>(
          U(png[static_cast<std::size_t>(pos) + 4 + i])));
    }
    const std::uint64_t data_begin = pos + 8;
    const std::uint64_t data_end = data_begin + length;
    REQUIRE(data_end + 4 <= png.size());
    chunk.data.assign(png.begin() + static_cast<std::ptrdiff_t>(data_begin),
                      png.begin() + static_cast<std::ptrdiff_t>(data_end));
    chunk.stored_crc = read_u32_be(png, static_cast<std::size_t>(data_end));
    chunks.push_back(std::move(chunk));
    pos = data_end + 4;
  }
  REQUIRE(pos == png.size());
  return chunks;
}

const TestChunk& find_chunk(const std::vector<TestChunk>& chunks,
                            const std::string& type) {
  for (const auto& chunk : chunks) {
    if (chunk.type == type) {
      return chunk;
    }
  }
  FAIL("chunk not found: " << type);
  static TestChunk missing;
  return missing;
}

void require_valid_crcs(const std::vector<TestChunk>& chunks) {
  for (const auto& chunk : chunks) {
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(chunk.type.data()), 4);
    if (!chunk.data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(chunk.data.data()),
                  static_cast<uInt>(chunk.data.size()));
    }
    INFO("chunk " << chunk.type);
    REQUIRE(static_cast<std::uint32_t>(crc) == chunk.stored_crc);
  }
}

void require_chunk_order(const std::vector<TestChunk>& chunks,
                         const std::vector<std::string>& expected) {
  REQUIRE(chunks.size() == expected.size());
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    INFO("chunk " << i);
    REQUIRE(chunks[i].type == expected[i]);
  }
}

struct Ihdr {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;
  std::uint8_t color_type = 0;
  std::uint8_t compression = 0;
  std::uint8_t filter_method = 0;
  std::uint8_t interlace = 0;
};

Ihdr parse_ihdr(const TestChunk& chunk) {
  REQUIRE(chunk.data.size() == 13);
  Ihdr ihdr;
  ihdr.width = read_u32_be(chunk.data, 0);
  ihdr.height = read_u32_be(chunk.data, 4);
  ihdr.bit_depth = U(chunk.data[8]);
  ihdr.color_type = U(chunk.data[9]);
  ihdr.compression = U(chunk.data[10]);
  ihdr.filter_method = U(chunk.data[11]);
  ihdr.interlace = U(chunk.data[12]);
  return ihdr;
}

// Decompresses the single-IDAT zlib stream into exactly `expected_size` bytes.
std::vector<std::byte> inflate_idat(const std::vector<TestChunk>& chunks,
                                    std::size_t expected_size) {
  const TestChunk& idat = find_chunk(chunks, "IDAT");
  std::vector<std::byte> out(expected_size + 1);
  uLongf out_size = static_cast<uLongf>(out.size());
  const int rc = uncompress(
      reinterpret_cast<Bytef*>(out.data()), &out_size,
      reinterpret_cast<const Bytef*>(idat.data.data()),
      static_cast<uLong>(idat.data.size()));
  REQUIRE(rc == Z_OK);
  out.resize(out_size);
  return out;
}

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

// Independent non-interlaced unfilter (PNG spec §6, filters 0-4).
std::vector<std::byte> unfilter_noninterlaced(
    const std::vector<std::byte>& filtered, std::uint32_t width,
    std::uint32_t height, std::size_t bpp, std::size_t row_bytes) {
  REQUIRE(filtered.size() == (1 + row_bytes) * height);
  std::vector<std::byte> raw(row_bytes * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t filter = U(filtered[y * (1 + row_bytes)]);
    REQUIRE(filter <= 4);
    for (std::size_t x = 0; x < row_bytes; ++x) {
      const std::size_t at = y * (1 + row_bytes) + 1 + x;
      const int left =
          x >= bpp ? static_cast<int>(U(raw[y * row_bytes + x - bpp])) : 0;
      const int up =
          y > 0 ? static_cast<int>(U(raw[(y - 1) * row_bytes + x])) : 0;
      const int up_left =
          x >= bpp && y > 0
              ? static_cast<int>(U(raw[(y - 1) * row_bytes + x - bpp]))
              : 0;
      int value = static_cast<int>(U(filtered[at]));
      switch (filter) {
        case 0:
          break;
        case 1:
          value += left;
          break;
        case 2:
          value += up;
          break;
        case 3:
          value += (left + up) / 2;
          break;
        default:
          value += paeth_predictor(left, up, up_left);
          break;
      }
      raw[y * row_bytes + x] = B(static_cast<unsigned>(value) & 0xFFu);
    }
  }
  return raw;
}

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

// Independent Adam7 defilter: returns w*h pixels of `bpp` bytes each.
std::vector<std::byte> unfilter_adam7(const std::vector<std::byte>& filtered,
                                      std::uint32_t width,
                                      std::uint32_t height, std::size_t bpp) {
  std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * bpp);
  std::size_t offset = 0;
  std::size_t consumed = 0;
  for (unsigned pass = 0; pass < 7; ++pass) {
    const auto& geo = kAdam7Passes[pass];
    const std::uint32_t pass_width =
        width > geo.x_start
            ? (width - geo.x_start + geo.x_step - 1) / geo.x_step
            : 0;
    const std::uint32_t pass_height =
        height > geo.y_start
            ? (height - geo.y_start + geo.y_step - 1) / geo.y_step
            : 0;
    if (pass_width == 0 || pass_height == 0) {
      continue;
    }
    const std::size_t row_bytes = static_cast<std::size_t>(pass_width) * bpp;
    const std::size_t pass_size = (1 + row_bytes) * pass_height;
    REQUIRE(filtered.size() >= offset + pass_size);
    const std::vector<std::byte> pass_pixels = unfilter_noninterlaced(
        {filtered.begin() + static_cast<std::ptrdiff_t>(offset),
         filtered.begin() + static_cast<std::ptrdiff_t>(offset + pass_size)},
        pass_width, pass_height, bpp, row_bytes);
    offset += pass_size;
    consumed += pass_size;
    for (std::uint32_t py = 0; py < pass_height; ++py) {
      for (std::uint32_t px = 0; px < pass_width; ++px) {
        const std::uint32_t x = geo.x_start + px * geo.x_step;
        const std::uint32_t y = geo.y_start + py * geo.y_step;
        for (std::size_t c = 0; c < bpp; ++c) {
          pixels[(static_cast<std::size_t>(y) * width + x) * bpp + c] =
              pass_pixels[(static_cast<std::size_t>(py) * pass_width + px) *
                              bpp +
                          c];
        }
      }
    }
  }
  REQUIRE(filtered.size() == consumed);
  return pixels;
}

struct LibpngImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  unsigned channels = 0;
  unsigned bit_depth = 0;
  std::vector<std::byte> bytes;
};

struct ReadContext {
  const std::byte* data;
  std::size_t size;
  std::size_t offset;
};

void read_callback(png_structp png, png_bytep out, png_size_t count) {
  auto* context = static_cast<ReadContext*>(png_get_io_ptr(png));
  REQUIRE(context->offset + count <= context->size);
  std::memcpy(out, context->data + context->offset, count);
  context->offset += count;
}

LibpngImage decode_with_libpng(const std::vector<std::byte>& png,
                               bool expand_to_8bit) {
  png_structp reader =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  REQUIRE(reader != nullptr);
  png_infop info = png_create_info_struct(reader);
  REQUIRE(info != nullptr);
  LibpngImage image;
  ReadContext context{png.data(), png.size(), 0};
  if (setjmp(png_jmpbuf(reader)) != 0) {
    FAIL("libpng failed to decode the fixture");
  }
  png_set_read_fn(reader, &context, read_callback);
  png_read_info(reader, info);
  if (expand_to_8bit) {
    png_set_palette_to_rgb(reader);
    png_set_expand_gray_1_2_4_to_8(reader);
    png_set_tRNS_to_alpha(reader);
    png_set_packing(reader);
  }
  (void)png_set_interlace_handling(reader);
  png_read_update_info(reader, info);
  image.width = static_cast<std::uint32_t>(png_get_image_width(reader, info));
  image.height = static_cast<std::uint32_t>(png_get_image_height(reader, info));
  image.channels = png_get_channels(reader, info);
  image.bit_depth = png_get_bit_depth(reader, info);
  const std::size_t row_bytes = png_get_rowbytes(reader, info);
  image.bytes.resize(row_bytes * image.height);
  std::vector<png_bytep> rows(image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    rows[y] = reinterpret_cast<png_bytep>(image.bytes.data() + y * row_bytes);
  }
  png_read_image(reader, rows.data());
  png_read_end(reader, nullptr);
  png_destroy_read_struct(&reader, &info, nullptr);
  return image;
}

const pnga_test::wp607c::ImageFacts& require_image(
    const ControlledFixture& fixture) {
  REQUIRE(fixture.expected.image.has_value());
  return *fixture.expected.image;
}

}  // namespace

TEST_CASE("WP-607C pixel cases expose fixed image facts", "[wp607c][corpus]") {
  using namespace pnga_test::wp607c;
  const auto gray = make_controlled_fixture(ControlledCaseId::kUiGray1None);
  REQUIRE(gray.stable_id == "ui-gray1-none");
  REQUIRE(gray.expected.image->width == 9);
  REQUIRE(gray.expected.image->height == 3);
  REQUIRE(gray.expected.image->bit_depth == 1);
  REQUIRE(gray.expected.image->color_type == 0);
  REQUIRE(gray.expected.image->row_filters == std::vector<std::uint8_t>{0,0,0});

  const auto indexed = make_controlled_fixture(ControlledCaseId::kUiIndexed4Trns);
  REQUIRE(indexed.expected.image->bit_depth == 4);
  REQUIRE(indexed.expected.image->color_type == 3);
  REQUIRE(indexed.expected.image->palette_entries.size() == 16);
  REQUIRE(indexed.expected.image->alpha_entries.size() == 16);

  const auto filters = make_controlled_fixture(ControlledCaseId::kUiRgb8FiveFilters);
  REQUIRE(filters.expected.image->row_filters == std::vector<std::uint8_t>{0,1,2,3,4});

  const auto rgba16 = make_controlled_fixture(ControlledCaseId::kUiRgba16ByteSelect);
  REQUIRE(rgba16.expected.image->selected_sample_bytes ==
          std::vector<std::uint8_t>{0x12, 0x34});

  const auto adam7 = make_controlled_fixture(ControlledCaseId::kUiAdam7EmptyPasses);
  REQUIRE(adam7.expected.image->interlace == 1);
  REQUIRE(adam7.expected.image->empty_passes ==
          std::vector<std::uint8_t>{1,2,3,4,6});
}

TEST_CASE("WP-607C registry lists nineteen unique stable ids",
          "[wp607c][corpus]") {
  const auto all = pnga_test::wp607c::all_controlled_cases();
  REQUIRE(all.size() == 19);
  std::set<int> ids;
  for (const auto id : all) {
    ids.insert(static_cast<int>(id));
  }
  REQUIRE(ids.size() == 19);
  REQUIRE(pnga_test::wp607c::controlled_case_id("ui-gray1-none") ==
          ControlledCaseId::kUiGray1None);
  REQUIRE(pnga_test::wp607c::controlled_case_id("perf-large-rgba8") ==
          ControlledCaseId::kPerfLargeRgba8);
  REQUIRE_FALSE(
      pnga_test::wp607c::controlled_case_id("no-such-case").has_value());
}

TEST_CASE("ui-gray1-none chunk stream and pixels are exact",
          "[wp607c][corpus]") {
  const auto fixture = make_controlled_fixture(ControlledCaseId::kUiGray1None);
  const auto& image = require_image(fixture);
  REQUIRE(image.width == 9);
  REQUIRE(image.height == 3);
  REQUIRE(image.bit_depth == 1);
  REQUIRE(image.color_type == 0);
  REQUIRE(image.interlace == 0);

  const auto chunks = parse_chunks(fixture.png_bytes);
  require_chunk_order(chunks, {"IHDR", "IDAT", "IEND"});
  require_valid_crcs(chunks);
  const auto ihdr = parse_ihdr(find_chunk(chunks, "IHDR"));
  REQUIRE(ihdr.width == 9);
  REQUIRE(ihdr.height == 3);
  REQUIRE(ihdr.bit_depth == 1);
  REQUIRE(ihdr.color_type == 0);
  REQUIRE(ihdr.compression == 0);
  REQUIRE(ihdr.filter_method == 0);
  REQUIRE(ihdr.interlace == 0);

  // Filter byte + two packed bytes per row: value(x, y) = (x + y) % 2 gives
  // 01010101 0 / 10101010 1 / 01010101 0.
  const std::vector<std::byte> expected_filtered = {
      B(0x00), B(0x55), B(0x00),
      B(0x00), B(0xAA), B(0x80),
      B(0x00), B(0x55), B(0x00),
  };
  REQUIRE(inflate_idat(chunks, expected_filtered.size()) == expected_filtered);

  // Delivered pixels, declared independently: value(x, y) = (x + y) % 2.
  const LibpngImage oracle = decode_with_libpng(fixture.png_bytes, true);
  REQUIRE(oracle.width == 9);
  REQUIRE(oracle.height == 3);
  REQUIRE(oracle.channels == 1);
  REQUIRE(oracle.bit_depth == 8);
  for (std::uint32_t y = 0; y < 3; ++y) {
    for (std::uint32_t x = 0; x < 9; ++x) {
      const unsigned expected =
          ((x + y) % 2) != 0 ? 255u : 0u;
      INFO("pixel " << x << "," << y);
      REQUIRE(U(oracle.bytes[y * 9 + x]) == expected);
    }
  }
}

TEST_CASE("ui-indexed4-trns chunk stream and pixels are exact",
          "[wp607c][corpus]") {
  const auto fixture =
      make_controlled_fixture(ControlledCaseId::kUiIndexed4Trns);
  const auto& image = require_image(fixture);
  REQUIRE(image.width == 5);
  REQUIRE(image.height == 3);
  REQUIRE(image.bit_depth == 4);
  REQUIRE(image.color_type == 3);
  REQUIRE(image.interlace == 0);
  REQUIRE(image.row_filters == std::vector<std::uint8_t>{0, 0, 0});

  const auto chunks = parse_chunks(fixture.png_bytes);
  require_chunk_order(chunks, {"IHDR", "PLTE", "tRNS", "IDAT", "IEND"});
  require_valid_crcs(chunks);
  const auto ihdr = parse_ihdr(find_chunk(chunks, "IHDR"));
  REQUIRE(ihdr.width == 5);
  REQUIRE(ihdr.height == 3);
  REQUIRE(ihdr.bit_depth == 4);
  REQUIRE(ihdr.color_type == 3);
  REQUIRE(ihdr.interlace == 0);

  const auto& palette = image.palette_entries;
  const auto& alpha = image.alpha_entries;
  const TestChunk& plte = find_chunk(chunks, "PLTE");
  REQUIRE(plte.data.size() == palette.size() * 3);
  for (std::size_t i = 0; i < palette.size(); ++i) {
    INFO("palette entry " << i);
    REQUIRE(U(plte.data[i * 3]) == palette[i][0]);
    REQUIRE(U(plte.data[i * 3 + 1]) == palette[i][1]);
    REQUIRE(U(plte.data[i * 3 + 2]) == palette[i][2]);
  }
  const TestChunk& trns = find_chunk(chunks, "tRNS");
  REQUIRE(trns.data.size() == alpha.size());
  for (std::size_t i = 0; i < alpha.size(); ++i) {
    INFO("alpha entry " << i);
    REQUIRE(U(trns.data[i]) == alpha[i]);
  }

  // Filter byte + three packed nibble bytes per row; index(x, y) = x + 5y.
  const std::vector<std::byte> expected_filtered = {
      B(0x00), B(0x01), B(0x23), B(0x40),
      B(0x00), B(0x56), B(0x78), B(0x90),
      B(0x00), B(0xAB), B(0xCD), B(0xE0),
  };
  REQUIRE(inflate_idat(chunks, expected_filtered.size()) == expected_filtered);

  // Delivered pixels through the oracle: palette[index] with per-index alpha.
  const LibpngImage oracle = decode_with_libpng(fixture.png_bytes, true);
  REQUIRE(oracle.width == 5);
  REQUIRE(oracle.height == 3);
  REQUIRE(oracle.channels == 4);
  REQUIRE(oracle.bit_depth == 8);
  for (std::uint32_t y = 0; y < 3; ++y) {
    for (std::uint32_t x = 0; x < 5; ++x) {
      const std::size_t index = x + 5 * y;
      const std::size_t at = (y * 5 + x) * 4;
      INFO("pixel " << x << "," << y << " index " << index);
      REQUIRE(U(oracle.bytes[at]) == palette[index][0]);
      REQUIRE(U(oracle.bytes[at + 1]) == palette[index][1]);
      REQUIRE(U(oracle.bytes[at + 2]) == palette[index][2]);
      REQUIRE(U(oracle.bytes[at + 3]) == alpha[index]);
    }
  }
}

TEST_CASE("ui-rgb8-five-filters chunk stream and pixels are exact",
          "[wp607c][corpus]") {
  const auto fixture =
      make_controlled_fixture(ControlledCaseId::kUiRgb8FiveFilters);
  const auto& image = require_image(fixture);
  REQUIRE(image.width == 8);
  REQUIRE(image.height == 5);
  REQUIRE(image.bit_depth == 8);
  REQUIRE(image.color_type == 2);
  REQUIRE(image.interlace == 0);
  REQUIRE(image.row_filters == std::vector<std::uint8_t>{0, 1, 2, 3, 4});

  const auto chunks = parse_chunks(fixture.png_bytes);
  require_chunk_order(chunks, {"IHDR", "IDAT", "IEND"});
  require_valid_crcs(chunks);
  const auto ihdr = parse_ihdr(find_chunk(chunks, "IHDR"));
  REQUIRE(ihdr.width == 8);
  REQUIRE(ihdr.height == 5);
  REQUIRE(ihdr.bit_depth == 8);
  REQUIRE(ihdr.color_type == 2);
  REQUIRE(ihdr.interlace == 0);

  // One row per filter (0,1,2,3,4) over fixed raw rows:
  //   y0 = 8x{00,11,22}, y1 = y2 = 8x{10,20,30}, y3 = y4 = 8x{20,20,20}.
  const std::vector<std::byte> row0_unit = {B(0x00), B(0x11), B(0x22)};
  const std::vector<std::byte> row1_unit = {B(0x10), B(0x20), B(0x30)};
  const std::vector<std::byte> row3_unit = {B(0x20), B(0x20), B(0x20)};
  const auto repeat8 = [](const std::vector<std::byte>& unit) {
    std::vector<std::byte> out;
    for (unsigned i = 0; i < 8; ++i) {
      out.insert(out.end(), unit.begin(), unit.end());
    }
    return out;
  };
  std::vector<std::byte> expected_filtered;
  expected_filtered.push_back(B(0x00));  // y0: None -> raw bytes
  {
    const auto raw = repeat8(row0_unit);
    expected_filtered.insert(expected_filtered.end(), raw.begin(), raw.end());
  }
  expected_filtered.push_back(B(0x01));  // y1: Sub -> first pixel, then zeros
  {
    expected_filtered.insert(expected_filtered.end(), row1_unit.begin(),
                             row1_unit.end());
    expected_filtered.insert(expected_filtered.end(), 21, B(0x00));
  }
  expected_filtered.push_back(B(0x02));  // y2: Up -> identical row, all zeros
  expected_filtered.insert(expected_filtered.end(), 24, B(0x00));
  // y3: Average over prior raw row 8x{10,20,30}: per pixel the deltas are
  // 0x18, 0x10, 0x08 (pixel 0) and then 0x08, 0x00, 0xF8.
  expected_filtered.push_back(B(0x03));
  for (const unsigned delta : {0x18u, 0x10u, 0x08u}) {
    expected_filtered.push_back(B(delta));
  }
  for (unsigned pixel = 1; pixel < 8; ++pixel) {
    for (const unsigned delta : {0x08u, 0x00u, 0xF8u}) {
      expected_filtered.push_back(B(delta));
    }
  }
  expected_filtered.push_back(B(0x04));  // y4: Paeth -> all zeros
  expected_filtered.insert(expected_filtered.end(), 24, B(0x00));

  const std::vector<std::byte> filtered =
      inflate_idat(chunks, expected_filtered.size());
  REQUIRE(filtered == expected_filtered);

  // Delivered pixels, declared independently of the filtered bytes above.
  const std::vector<std::byte> expected_raw = [&] {
    std::vector<std::byte> raw = repeat8(row0_unit);
    for (unsigned y = 1; y <= 2; ++y) {
      const auto row = repeat8(row1_unit);
      raw.insert(raw.end(), row.begin(), row.end());
    }
    for (unsigned y = 3; y <= 4; ++y) {
      const auto row = repeat8(row3_unit);
      raw.insert(raw.end(), row.begin(), row.end());
    }
    return raw;
  }();
  const std::vector<std::byte> delivered =
      unfilter_noninterlaced(filtered, 8, 5, 3, 24);
  REQUIRE(delivered == expected_raw);

  const LibpngImage oracle = decode_with_libpng(fixture.png_bytes, false);
  REQUIRE(oracle.width == 8);
  REQUIRE(oracle.height == 5);
  REQUIRE(oracle.channels == 3);
  REQUIRE(oracle.bit_depth == 8);
  REQUIRE(oracle.bytes == expected_raw);
}

TEST_CASE("ui-rgba16-byte-select chunk stream and pixels are exact",
          "[wp607c][corpus]") {
  const auto fixture =
      make_controlled_fixture(ControlledCaseId::kUiRgba16ByteSelect);
  const auto& image = require_image(fixture);
  REQUIRE(image.width == 3);
  REQUIRE(image.height == 2);
  REQUIRE(image.bit_depth == 16);
  REQUIRE(image.color_type == 6);
  REQUIRE(image.interlace == 0);
  REQUIRE(image.row_filters == std::vector<std::uint8_t>{0, 0});
  REQUIRE(image.selected_sample_bytes ==
          std::vector<std::uint8_t>{0x12, 0x34});

  const auto chunks = parse_chunks(fixture.png_bytes);
  require_chunk_order(chunks, {"IHDR", "IDAT", "IEND"});
  require_valid_crcs(chunks);
  const auto ihdr = parse_ihdr(find_chunk(chunks, "IHDR"));
  REQUIRE(ihdr.width == 3);
  REQUIRE(ihdr.height == 2);
  REQUIRE(ihdr.bit_depth == 16);
  REQUIRE(ihdr.color_type == 6);
  REQUIRE(ihdr.interlace == 0);

  // Big-endian channel values r/g/b/a = 0x1234/0x5678/0x9ABC/0xDEF0 plus the
  // linear sample offset (x + 3y); filters are 0, so filtered bytes are raw.
  const auto channel_bytes = [](unsigned value) {
    return std::vector<std::byte>{B((value >> 8) & 0xFFu), B(value & 0xFFu)};
  };
  std::vector<std::byte> expected_filtered;
  for (std::uint32_t y = 0; y < 2; ++y) {
    expected_filtered.push_back(B(0x00));
    for (std::uint32_t x = 0; x < 3; ++x) {
      const unsigned offset = x + 3 * y;
      for (const unsigned base : {0x1234u, 0x5678u, 0x9ABCu, 0xDEF0u}) {
        const auto bytes = channel_bytes(base + offset);
        expected_filtered.insert(expected_filtered.end(), bytes.begin(),
                                 bytes.end());
      }
    }
  }
  const std::vector<std::byte> filtered =
      inflate_idat(chunks, expected_filtered.size());
  REQUIRE(filtered == expected_filtered);
  // The selected sample is pixel (0,0) red: high byte 0x12, low byte 0x34.
  REQUIRE(U(filtered[1]) == 0x12);
  REQUIRE(U(filtered[2]) == 0x34);

  // Delivered pixels through the oracle: identical big-endian 16-bit rows
  // (both row filter bytes removed from the filtered stream).
  std::vector<std::byte> expected_raw;
  expected_raw.insert(expected_raw.end(), filtered.begin() + 1,
                      filtered.begin() + 25);
  expected_raw.insert(expected_raw.end(), filtered.begin() + 26,
                      filtered.end());
  const LibpngImage oracle = decode_with_libpng(fixture.png_bytes, false);
  REQUIRE(oracle.width == 3);
  REQUIRE(oracle.height == 2);
  REQUIRE(oracle.channels == 4);
  REQUIRE(oracle.bit_depth == 16);
  REQUIRE(oracle.bytes == expected_raw);
}

TEST_CASE("ui-adam7-empty-passes chunk stream and pixels are exact",
          "[wp607c][corpus]") {
  const auto fixture =
      make_controlled_fixture(ControlledCaseId::kUiAdam7EmptyPasses);
  const auto& image = require_image(fixture);
  REQUIRE(image.width == 2);
  REQUIRE(image.height == 1);
  REQUIRE(image.bit_depth == 8);
  REQUIRE(image.color_type == 6);
  REQUIRE(image.interlace == 1);
  // Zero-based empty passes for 2x1: specification passes 2, 3, 4, 5 and 7.
  REQUIRE(image.empty_passes == std::vector<std::uint8_t>{1, 2, 3, 4, 6});
  REQUIRE(image.row_filters == std::vector<std::uint8_t>{0, 0});

  const auto chunks = parse_chunks(fixture.png_bytes);
  require_chunk_order(chunks, {"IHDR", "IDAT", "IEND"});
  require_valid_crcs(chunks);
  const auto ihdr = parse_ihdr(find_chunk(chunks, "IHDR"));
  REQUIRE(ihdr.width == 2);
  REQUIRE(ihdr.height == 1);
  REQUIRE(ihdr.bit_depth == 8);
  REQUIRE(ihdr.color_type == 6);
  REQUIRE(ihdr.interlace == 1);

  // Only passes 1 and 6 (zero-based 0 and 5) carry the single pixels.
  const std::vector<std::byte> expected_filtered = {
      B(0x00), B(0x10), B(0x20), B(0x30), B(0x40),
      B(0x00), B(0x50), B(0x60), B(0x70), B(0x80),
  };
  REQUIRE(inflate_idat(chunks, expected_filtered.size()) == expected_filtered);

  // Delivered pixels, declared independently: (0,0) and (1,0) in that order.
  const std::vector<std::byte> expected_pixels = {
      B(0x10), B(0x20), B(0x30), B(0x40),
      B(0x50), B(0x60), B(0x70), B(0x80),
  };
  const std::vector<std::byte> delivered =
      unfilter_adam7(expected_filtered, 2, 1, 4);
  REQUIRE(delivered == expected_pixels);

  const LibpngImage oracle = decode_with_libpng(fixture.png_bytes, false);
  REQUIRE(oracle.width == 2);
  REQUIRE(oracle.height == 1);
  REQUIRE(oracle.channels == 4);
  REQUIRE(oracle.bit_depth == 8);
  REQUIRE(oracle.bytes == expected_pixels);
}
