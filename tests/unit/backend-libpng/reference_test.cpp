// WP-202 Reference Backend tests. Fixtures are encoded in-memory with the
// public libpng write API (test oracle), then decoded with the backend.

#include <pnga/backend-libpng/reference.h>

#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <png.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using pnga::backend_libpng::decode_reference;
using pnga::backend_libpng::libpng_version;
using pnga::backend_libpng::ReferenceResult;
using pnga::io::MemoryByteSource;

namespace {

struct WriteBuffer {
  std::vector<std::byte> bytes;
};

void write_callback(png_structp png, png_bytep data, png_size_t length) {
  auto* buf = static_cast<WriteBuffer*>(png_get_io_ptr(png));
  auto* begin = reinterpret_cast<std::byte*>(data);
  buf->bytes.insert(buf->bytes.end(), begin, begin + length);
}

void flush_callback(png_structp) {}

// Encodes a PNG with the given color type / bit depth / interlace. `raw` holds
// one full image worth of native pixels in libpng's row layout for that format.
std::vector<std::byte> encode_png(std::uint32_t width, std::uint32_t height,
                                  int color_type, int bit_depth, bool interlace,
                                  const std::vector<std::byte>& raw) {
  WriteBuffer buf;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  REQUIRE(png != nullptr);
  png_infop info = png_create_info_struct(png);
  REQUIRE(info != nullptr);
  const int jmp_status = setjmp(png_jmpbuf(png));
  REQUIRE(jmp_status == 0);

  png_set_write_fn(png, &buf, write_callback, flush_callback);
  png_set_IHDR(png, info, width, height, bit_depth, color_type,
               interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  const png_size_t row_bytes = png_get_rowbytes(png, info);
  std::vector<png_bytep> rows(height);
  for (std::uint32_t y = 0; y < height; ++y) {
    rows[y] = reinterpret_cast<png_bytep>(
        const_cast<std::byte*>(raw.data() + y * row_bytes));
  }

  png_write_info(png, info);
  png_write_image(png, rows.data());
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  return buf.bytes;
}

// Solid-color raw image for a given channels-per-pixel byte layout.
std::vector<std::byte> solid_raw(std::uint32_t width, std::uint32_t height,
                                 int channels, unsigned char r,
                                 unsigned char g, unsigned char b,
                                 unsigned char a) {
  std::vector<std::byte> raw(static_cast<std::size_t>(width) * height * channels);
  for (std::size_t i = 0; i + channels <= raw.size(); i += channels) {
    if (channels >= 1) raw[i] = static_cast<std::byte>(r);
    if (channels >= 2) raw[i + 1] = static_cast<std::byte>(g);
    if (channels >= 3) raw[i + 2] = static_cast<std::byte>(b);
    if (channels == 4) raw[i + 3] = static_cast<std::byte>(a);
  }
  return raw;
}

std::byte at(const ReferenceResult& r, std::uint32_t x, std::uint32_t y,
             std::size_t channel) {
  return r.image.rgba[(static_cast<std::size_t>(y) * r.image.width + x) * 4 +
                      channel];
}

}  // namespace

TEST_CASE("libpng_version matches the linked library", "[backend-libpng][wp202]") {
  REQUIRE(std::string(libpng_version()) == std::string(PNG_LIBPNG_VER_STRING));
}

TEST_CASE("Decodes an RGB8 image to RGBA8", "[backend-libpng][wp202]") {
  const auto png = encode_png(4, 3, PNG_COLOR_TYPE_RGB, 8, false,
                              solid_raw(4, 3, 3, 10, 20, 30, 0));
  MemoryByteSource src(png);
  const ReferenceResult r = decode_reference(src);

  INFO("decode error: " << r.error);
  REQUIRE(r.success);
  REQUIRE(r.image.width == 4);
  REQUIRE(r.image.height == 3);
  REQUIRE(r.image.source_color_type == PNG_COLOR_TYPE_RGB);
  REQUIRE(r.image.source_bit_depth == 8);
  REQUIRE_FALSE(r.image.interlaced);
  REQUIRE(r.image.rgba.size() == 4 * 3 * 4);
  // RGB -> RGBA with opaque alpha filler.
  REQUIRE(at(r, 0, 0, 0) == std::byte{10});
  REQUIRE(at(r, 0, 0, 1) == std::byte{20});
  REQUIRE(at(r, 0, 0, 2) == std::byte{30});
  REQUIRE(at(r, 0, 0, 3) == std::byte{0xFF});
}

TEST_CASE("Decodes an RGBA8 image preserving alpha", "[backend-libpng][wp202]") {
  const auto png = encode_png(2, 2, PNG_COLOR_TYPE_RGB_ALPHA, 8, false,
                              solid_raw(2, 2, 4, 1, 2, 3, 128));
  MemoryByteSource src(png);
  const ReferenceResult r = decode_reference(src);
  REQUIRE(r.success);
  REQUIRE(at(r, 1, 1, 3) == std::byte{128});
}

TEST_CASE("Decodes a paletted image via expand", "[backend-libpng][wp202]") {
  // Build a palette PNG manually through libpng set_* calls.
  WriteBuffer buf;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  REQUIRE(png != nullptr);
  png_infop info = png_create_info_struct(png);
  REQUIRE(info != nullptr);
  const int jmp_status = setjmp(png_jmpbuf(png));
  REQUIRE(jmp_status == 0);
  png_set_write_fn(png, &buf, write_callback, flush_callback);
  png_set_IHDR(png, info, 2, 1, 8, PNG_COLOR_TYPE_PALETTE,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_color palette[2] = {{0, 255, 0}, {255, 0, 0}};
  png_set_PLTE(png, info, palette, 2);
  const std::byte index_bytes[] = {std::byte{0}, std::byte{1}};

  png_bytep rows[1] = {reinterpret_cast<png_bytep>(
      const_cast<std::byte*>(index_bytes))};
  png_write_info(png, info);
  png_write_image(png, rows);
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);

  MemoryByteSource src(buf.bytes);
  const ReferenceResult r = decode_reference(src);
  REQUIRE(r.success);
  REQUIRE(r.image.source_color_type == PNG_COLOR_TYPE_PALETTE);
  REQUIRE(at(r, 0, 0, 0) == std::byte{0});
  REQUIRE(at(r, 0, 0, 1) == std::byte{255});
  REQUIRE(at(r, 1, 0, 0) == std::byte{255});
  REQUIRE(at(r, 1, 0, 1) == std::byte{0});
}

TEST_CASE("Decodes a 16-bit image stripped to 8-bit", "[backend-libpng][wp202]") {
  // 16-bit grayscale: one 2x2 image with value 0x1234 -> expanded+stripped.
  WriteBuffer buf;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  REQUIRE(png != nullptr);
  png_infop info = png_create_info_struct(png);
  REQUIRE(info != nullptr);
  const int jmp_status = setjmp(png_jmpbuf(png));
  REQUIRE(jmp_status == 0);
  png_set_write_fn(png, &buf, write_callback, flush_callback);
  png_set_IHDR(png, info, 2, 2, 16, PNG_COLOR_TYPE_GRAY,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  // Two 16-bit big-endian samples per pixel? Gray16 = 2 bytes per pixel.
  const std::byte gray[8] = {std::byte{0x12}, std::byte{0x34},
                             std::byte{0x12}, std::byte{0x34},
                             std::byte{0x12}, std::byte{0x34},
                             std::byte{0x12}, std::byte{0x34}};
  png_write_info(png, info);
  png_bytep rows[2] = {
      reinterpret_cast<png_bytep>(const_cast<std::byte*>(gray)),
      reinterpret_cast<png_bytep>(const_cast<std::byte*>(gray)) + 4};
  png_write_image(png, rows);
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);

  MemoryByteSource src(buf.bytes);
  const ReferenceResult r = decode_reference(src);
  REQUIRE(r.success);
  REQUIRE(r.image.source_bit_depth == 16);
  // 0x1234 -> strip_16 keeps the high byte 0x12, gray->RGB + filler -> RGBA.
  REQUIRE(at(r, 0, 0, 0) == std::byte{0x12});
  REQUIRE(at(r, 0, 0, 3) == std::byte{0xFF});
}

TEST_CASE("Decodes an interlaced image to full dimensions",
          "[backend-libpng][wp202]") {
  const auto png = encode_png(5, 5, PNG_COLOR_TYPE_RGB, 8, true,
                              solid_raw(5, 5, 3, 7, 8, 9, 0));
  MemoryByteSource src(png);
  const ReferenceResult r = decode_reference(src);
  REQUIRE(r.success);
  REQUIRE(r.image.interlaced);
  REQUIRE(r.image.width == 5);
  REQUIRE(r.image.height == 5);
  REQUIRE(at(r, 4, 4, 0) == std::byte{7});
  REQUIRE(at(r, 4, 4, 1) == std::byte{8});
}

TEST_CASE("Decodes 1-bit grayscale via expand", "[backend-libpng][wp202]") {
  WriteBuffer buf;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  REQUIRE(png != nullptr);
  png_infop info = png_create_info_struct(png);
  REQUIRE(info != nullptr);
  const int jmp_status = setjmp(png_jmpbuf(png));
  REQUIRE(jmp_status == 0);
  png_set_write_fn(png, &buf, write_callback, flush_callback);
  png_set_IHDR(png, info, 8, 1, 1, PNG_COLOR_TYPE_GRAY,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  const std::byte row_bytes[1] = {std::byte{0b10110000}};
  png_write_info(png, info);
  png_bytep row = reinterpret_cast<png_bytep>(const_cast<std::byte*>(row_bytes));
  png_write_image(png, &row);
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);

  MemoryByteSource src(buf.bytes);
  const ReferenceResult r = decode_reference(src);
  REQUIRE(r.success);
  REQUIRE(r.image.source_bit_depth == 1);
  REQUIRE(at(r, 0, 0, 0) == std::byte{0xFF});
  REQUIRE(at(r, 1, 0, 0) == std::byte{0x00});
}

TEST_CASE("Malformed input fails cleanly without crashing",
          "[backend-libpng][wp202]") {
  const std::vector<std::byte> truncated = {
      std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}};
  MemoryByteSource src(truncated);
  const ReferenceResult r = decode_reference(src);
  REQUIRE_FALSE(r.success);
  REQUIRE_FALSE(r.error.empty());
  REQUIRE(r.image.empty());
}

TEST_CASE("Non-PNG bytes fail cleanly", "[backend-libpng][wp202]") {
  const std::vector<std::byte> junk(64, std::byte{0xAB});
  MemoryByteSource src(junk);
  const ReferenceResult r = decode_reference(src);
  REQUIRE_FALSE(r.success);
  REQUIRE_FALSE(r.error.empty());
}
