// WP-303 pass reconstruction tests: Adam7 pass unfiltering + placement into the
// final target coordinates. A test-side encoder builds real interlaced PNGs
// (independent spec geometry, forward filters, zlib compression + CRC32), so
// libpng's raw (no-transform) decoder acts as an independent oracle for the
// reconstructed packed target. The encoder never touches production code.

#include <pnga/png-reconstruction/pass_reconstruction.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-reconstruction/reverse_filter.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <catch2/catch_test_macros.hpp>

#include <png.h>
#include <zlib.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_reconstruction::compute_scanline_layout;
using pnga::png_reconstruction::FilterType;
using pnga::png_reconstruction::ImageHeader;
using pnga::png_reconstruction::PassReconstructionOutcome;
using pnga::png_reconstruction::reconstruct_image;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

// ---------------------------------------------------------------------------
// Independent Adam7 geometry (PNG spec §8.5), deliberately not the production
// layout code, so a geometry regression in WP-300 is not mirrored here.
// ---------------------------------------------------------------------------

struct TestPassGeom {
  std::uint64_t w;
  std::uint64_t h;
  std::uint64_t xs;
  std::uint64_t ys;
  std::uint64_t xstep;
  std::uint64_t ystep;
};

constexpr std::array<std::array<std::uint64_t, 4>, 7> kStartStep = {{
    {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
    {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2},
}};

std::vector<TestPassGeom> test_pass_geometry(std::uint32_t w, std::uint32_t h) {
  std::vector<TestPassGeom> g;
  for (const auto& ss : kStartStep) {
    std::uint64_t pw = w > ss[0] ? (w - ss[0] + ss[2] - 1) / ss[2] : 0;
    std::uint64_t ph = h > ss[1] ? (h - ss[1] + ss[3] - 1) / ss[3] : 0;
    if (pw == 0 || ph == 0) {
      // An empty Adam7 pass (zero width or height) contributes no pixels;
      // normalize both dimensions like the production layout does.
      pw = 0;
      ph = 0;
    }
    g.push_back(TestPassGeom{pw, ph, ss[0], ss[1], ss[2], ss[3]});
  }
  return g;
}

unsigned channels_of(std::uint8_t ct) {
  switch (ct) {
    case 0:
    case 3:
      return 1;
    case 2:
      return 3;
    case 4:
      return 2;
    case 6:
      return 4;
  }
  return 0;
}

std::uint64_t test_row_bytes(std::uint32_t w, std::uint8_t bd,
                             std::uint8_t ct) {
  return (static_cast<std::uint64_t>(w) * channels_of(ct) * bd + 7) / 8;
}

std::uint64_t test_bpp(std::uint8_t bd, std::uint8_t ct) {
  const std::uint64_t b = (channels_of(ct) * bd + 7) / 8;
  return b < 1 ? 1 : b;
}

std::uint8_t test_paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  const int p = static_cast<int>(a) + static_cast<int>(b) -
                static_cast<int>(c);
  const int pa = std::abs(p - static_cast<int>(a));
  const int pb = std::abs(p - static_cast<int>(b));
  const int pc = std::abs(p - static_cast<int>(c));
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

std::uint8_t test_predictor(FilterType f, std::uint8_t a, std::uint8_t b,
                            std::uint8_t c) {
  switch (f) {
    case FilterType::kNone:
      return 0;
    case FilterType::kSub:
      return a;
    case FilterType::kUp:
      return b;
    case FilterType::kAverage:
      return static_cast<std::uint8_t>(
          (static_cast<unsigned>(a) + static_cast<unsigned>(b)) / 2);
    case FilterType::kPaeth:
      return test_paeth(a, b, c);
  }
  return 0;
}

// Big-endian bit access (MSB-first, spec §2.2), independent of production.
std::uint8_t test_read_bits(const std::byte* data, std::uint64_t bit_pos,
                            unsigned bits) {
  std::uint8_t v = 0;
  for (unsigned k = 0; k < bits; ++k) {
    const std::uint64_t pos = bit_pos + k;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const std::uint8_t bit =
        static_cast<std::uint8_t>((static_cast<unsigned>(data[pos / 8]) >> shift) & 1u);
    v = static_cast<std::uint8_t>((v << 1) | bit);
  }
  return v;
}

void test_write_bits(std::byte* data, std::uint64_t bit_pos, unsigned bits,
                     std::uint8_t value) {
  for (unsigned k = 0; k < bits; ++k) {
    const std::uint64_t pos = bit_pos + k;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    const unsigned bit = (static_cast<unsigned>(value) >> (bits - 1 - k)) & 1u;
    data[pos / 8] = static_cast<std::byte>(
        (static_cast<unsigned>(data[pos / 8]) & ~(1u << shift)) |
        (bit << shift));
  }
}

// ---------------------------------------------------------------------------
// Raw pixel source. Every byte is nonzero (for bit_depth < 8 every sample is
// nonzero), so a placement gap (a left-over zero) is always visible.
// ---------------------------------------------------------------------------

std::vector<std::byte> make_raw_image(std::uint32_t w, std::uint32_t h,
                                      std::uint8_t bd, std::uint8_t ct,
                                      unsigned seed) {
  const std::uint64_t rb = test_row_bytes(w, bd, ct);
  std::vector<std::byte> raw(static_cast<std::size_t>(rb * h), std::byte{0});
  const unsigned ch = channels_of(ct);
  if (bd >= 8) {
    const unsigned bps = bd / 8;
    const std::uint64_t pix_bytes = static_cast<std::uint64_t>(ch) * bps;
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        for (unsigned c = 0; c < ch; ++c) {
          for (unsigned k = 0; k < bps; ++k) {
            const std::uint8_t v = static_cast<std::uint8_t>(
                1 + ((x * 3 + y * 5 + c * 7 + seed + k * 11) % 255));
            raw[static_cast<std::size_t>(y * rb + x * pix_bytes + c * bps + k)] =
                B(v);
          }
        }
      }
    }
  } else {
    const unsigned maxv = (1u << bd) - 1;  // 1, 3 or 15
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        const std::uint8_t v = static_cast<std::uint8_t>(
            1 + ((x * 3 + y * 5 + seed) % maxv));  // 1..maxv, never 0
        test_write_bits(raw.data() + y * rb, static_cast<std::uint64_t>(x) * bd,
                        bd, v);
      }
    }
  }
  return raw;
}

// ---------------------------------------------------------------------------
// Test-side PNG encoder: splits `raw` into passes (independent geometry),
// forward-filters every pass row (optional rotating filter set), zlib-compresses
// and wraps the result in a real PNG file with CRC32.
// ---------------------------------------------------------------------------

FilterType filter_for(std::uint64_t row_index, bool all_none) {
  if (all_none) {
    return FilterType::kNone;
  }
  static constexpr std::array<FilterType, 5> kAll = {
      FilterType::kNone, FilterType::kSub, FilterType::kUp,
      FilterType::kAverage, FilterType::kPaeth};
  return kAll[row_index % kAll.size()];
}

std::vector<std::byte> build_png_file(std::uint32_t w, std::uint32_t h,
                                      std::uint8_t bd, std::uint8_t ct,
                                      bool interlace,
                                      const std::vector<std::byte>& filtered) {
  constexpr std::array<std::byte, 8> kSig = {
      std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
      std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
  std::vector<std::byte> bytes(kSig.begin(), kSig.end());

  auto push_chunk = [&](const char* type, const std::vector<std::byte>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    bytes.push_back(B(static_cast<unsigned char>(len >> 24)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 16)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 8)));
    bytes.push_back(B(static_cast<unsigned char>(len)));
    uLong crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(B(static_cast<unsigned char>(type[i])));
    }
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.push_back(B(static_cast<unsigned char>(crc >> 24)));
    bytes.push_back(B(static_cast<unsigned char>(crc >> 16)));
    bytes.push_back(B(static_cast<unsigned char>(crc >> 8)));
    bytes.push_back(B(static_cast<unsigned char>(crc)));
  };

  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = B(static_cast<unsigned char>(w >> 24));
  ihdr[1] = B(static_cast<unsigned char>(w >> 16));
  ihdr[2] = B(static_cast<unsigned char>(w >> 8));
  ihdr[3] = B(static_cast<unsigned char>(w));
  ihdr[4] = B(static_cast<unsigned char>(h >> 24));
  ihdr[5] = B(static_cast<unsigned char>(h >> 16));
  ihdr[6] = B(static_cast<unsigned char>(h >> 8));
  ihdr[7] = B(static_cast<unsigned char>(h));
  ihdr[8] = B(bd);
  ihdr[9] = B(ct);
  ihdr[10] = B(0);  // compression
  ihdr[11] = B(0);  // filter
  ihdr[12] = B(interlace ? 1 : 0);
  push_chunk("IHDR", ihdr);

  if (ct == 3) {
    const unsigned n = 1u << bd;  // palette must hold at least 2^bit_depth
    std::vector<std::byte> plte;
    for (unsigned i = 0; i < n; ++i) {
      plte.push_back(B(static_cast<unsigned char>(i * 3 + 1)));
      plte.push_back(B(static_cast<unsigned char>(i * 5 + 2)));
      plte.push_back(B(static_cast<unsigned char>(i * 7 + 3)));
    }
    push_chunk("PLTE", plte);
  }

  uLongf bound = compressBound(static_cast<uLong>(filtered.size()));
  std::vector<std::byte> comp(static_cast<std::size_t>(bound));
  uLongf used = bound;
  const int rc =
      compress2(reinterpret_cast<Bytef*>(comp.data()), &used,
                reinterpret_cast<const Bytef*>(filtered.data()),
                static_cast<uLong>(filtered.size()), Z_DEFAULT_COMPRESSION);
  REQUIRE(rc == Z_OK);  // compressBound guarantees capacity
  comp.resize(static_cast<std::size_t>(used));
  push_chunk("IDAT", comp);
  push_chunk("IEND", {});
  return bytes;
}

struct EncodedPng {
  ImageHeader header;
  std::vector<std::byte> raw;      // original packed image bytes
  std::vector<std::byte> filtered; // pass-major flat filtered buffer
  std::vector<std::byte> png_bytes; // complete file for the libpng oracle
};

EncodedPng encode_png(std::uint32_t w, std::uint32_t h, std::uint8_t bd,
                      std::uint8_t ct, bool interlace, bool all_none,
                      unsigned seed = 7) {
  EncodedPng out;
  out.header = ImageHeader{w, h, bd, ct, interlace};
  out.raw = make_raw_image(w, h, bd, ct, seed);
  const std::uint64_t rb = test_row_bytes(w, bd, ct);
  const std::uint64_t bpp = test_bpp(bd, ct);
  const bool sub = bd < 8;

  const std::vector<TestPassGeom> geom =
      interlace ? test_pass_geometry(w, h)
                : std::vector<TestPassGeom>{{w, h, 0, 0, 1, 1}};

  std::uint64_t row_index = 0;
  for (const TestPassGeom& g : geom) {
    if (g.w == 0 || g.h == 0) {
      continue;
    }
    const std::uint64_t prb = test_row_bytes(static_cast<std::uint32_t>(g.w), bd, ct);
    std::vector<std::byte> prev;  // previous unfiltered row of this pass
    for (std::uint64_t j = 0; j < g.h; ++j) {
      // Build the pass row (packed) from the source image.
      std::vector<std::byte> prow(static_cast<std::size_t>(prb), std::byte{0});
      if (sub) {
        for (std::uint64_t sx = 0; sx < g.w; ++sx) {
          const std::uint32_t x =
              static_cast<std::uint32_t>(g.xs + sx * g.xstep);
          const std::uint32_t y =
              static_cast<std::uint32_t>(g.ys + j * g.ystep);
          const std::uint8_t v = test_read_bits(
              out.raw.data() + static_cast<std::uint64_t>(y) * rb,
              static_cast<std::uint64_t>(x) * bd, bd);
          test_write_bits(prow.data(), sx * bd, bd, v);
        }
      } else {
        const std::uint64_t pix_bytes =
            static_cast<std::uint64_t>(channels_of(ct)) * (bd / 8);
        for (std::uint64_t sx = 0; sx < g.w; ++sx) {
          const std::uint32_t x =
              static_cast<std::uint32_t>(g.xs + sx * g.xstep);
          const std::uint32_t y =
              static_cast<std::uint32_t>(g.ys + j * g.ystep);
          std::memcpy(prow.data() + sx * pix_bytes,
                      out.raw.data() + static_cast<std::uint64_t>(y) * rb +
                          static_cast<std::uint64_t>(x) * pix_bytes,
                      static_cast<std::size_t>(pix_bytes));
        }
      }
      // Forward-filter the packed pass row.
      const FilterType f = filter_for(row_index, all_none);
      out.filtered.push_back(B(static_cast<unsigned char>(f)));
      for (std::uint64_t i = 0; i < prb; ++i) {
        const std::uint8_t a =
            i >= bpp ? static_cast<std::uint8_t>(prow[static_cast<std::size_t>(i - bpp)]) : 0;
        const std::uint8_t b = (!prev.empty() && i < prev.size())
                                   ? static_cast<std::uint8_t>(prev[static_cast<std::size_t>(i)])
                                   : 0;
        const std::uint8_t c = (i >= bpp && !prev.empty() && i < prev.size())
                                   ? static_cast<std::uint8_t>(
                                         prev[static_cast<std::size_t>(i - bpp)])
                                   : 0;
        const std::uint8_t rawb = static_cast<std::uint8_t>(prow[static_cast<std::size_t>(i)]);
        out.filtered.push_back(B(static_cast<unsigned char>(rawb - test_predictor(f, a, b, c))));
      }
      prev = std::move(prow);
      ++row_index;
    }
  }

  out.png_bytes = build_png_file(w, h, bd, ct, interlace, out.filtered);
  return out;
}

// ---------------------------------------------------------------------------
// libpng raw oracle: decode the encoded file with zero transforms (packed rows
// at the file's own bit depth / color type, Adam7 fully assembled).
// ---------------------------------------------------------------------------

struct RawOracle {
  bool ok = false;
  std::string error;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  std::uint8_t bd = 0;
  std::uint8_t ct = 0;
  bool interlace = false;
  std::vector<std::byte> rows;
};

RawOracle raw_decode(const std::vector<std::byte>& png_bytes) {
  RawOracle out;
  MemoryByteSource src(png_bytes);
  struct ReadCtx {
    const IByteSource* s;
    std::uint64_t pos;
  };
  ReadCtx rctx{&src, 0};

  struct ErrState {
    std::string message;
  };
  ErrState est;

  auto error_fn = [](png_structp png, png_const_charp msg) {
    auto* e = static_cast<ErrState*>(png_get_error_ptr(png));
    if (msg != nullptr) {
      e->message = msg;
    }
    longjmp(png_jmpbuf(png), 1);
  };
  auto warning_fn = [](png_structp, png_const_charp) {};

  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, &est, error_fn, warning_fn);
  if (png == nullptr) {
    out.error = "libpng: png_create_read_struct failed";
    return out;
  }
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    out.error = "libpng: png_create_info_struct failed";
    return out;
  }

  if (setjmp(png_jmpbuf(png)) != 0) {
    out.error = est.message.empty() ? "libpng: decode error" : est.message;
    png_destroy_read_struct(&png, &info, nullptr);
    return out;
  }

  auto read_fn = [](png_structp p, png_bytep buf, png_size_t len) {
    auto* c = static_cast<ReadCtx*>(png_get_io_ptr(p));
    const std::uint64_t remaining =
        c->s->size() > c->pos ? c->s->size() - c->pos : 0;
    const std::size_t take =
        static_cast<std::size_t>(std::min<std::uint64_t>(len, remaining));
    if (take != 0) {
      c->s->read(c->pos, reinterpret_cast<std::byte*>(buf), take);
      c->pos += take;
    }
    std::memset(buf + take, 0, len - take);
  };
  png_set_read_fn(png, &rctx, read_fn);

  png_read_info(png, info);
  out.w = static_cast<std::uint32_t>(png_get_image_width(png, info));
  out.h = static_cast<std::uint32_t>(png_get_image_height(png, info));
  out.bd = static_cast<std::uint8_t>(png_get_bit_depth(png, info));
  out.ct = static_cast<std::uint8_t>(png_get_color_type(png, info));
  out.interlace = png_get_interlace_type(png, info) != PNG_INTERLACE_NONE;

  // png_read_image handles interlacing internally (calls png_set_interlace_handling
  // and assembles all passes), so no transform or update_info is needed here.
  const png_size_t rowbytes = png_get_rowbytes(png, info);
  const std::uint64_t total = static_cast<std::uint64_t>(rowbytes) * out.h;
  out.rows.resize(static_cast<std::size_t>(total));
  std::vector<png_bytep> rowptrs(out.h);
  for (std::uint32_t y = 0; y < out.h; ++y) {
    rowptrs[y] = reinterpret_cast<png_bytep>(
        out.rows.data() + static_cast<std::size_t>(y) * rowbytes);
  }
  png_read_image(png, rowptrs.data());
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);
  out.ok = true;
  return out;
}

// All legal (bit_depth, color_type) combinations.
constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 15> kCombos = {{
    {1, 0}, {2, 0}, {4, 0}, {8, 0}, {16, 0},
    {8, 2}, {16, 2},
    {1, 3}, {2, 3}, {4, 3}, {8, 3},
    {8, 4}, {16, 4},
    {8, 6}, {16, 6},
}};

}  // namespace

// ---------------------------------------------------------------------------
// A: exhaustive geometry — Adam7 round-trip over small sizes, every legal
// color type / bit depth. Uses None filters so the reconstructed target must
// equal the packed source bytes exactly (a placement gap leaves a zero).
// ---------------------------------------------------------------------------

TEST_CASE("Adam7 placement round-trips every legal header at small sizes",
          "[png-reconstruction][wp303]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 5}, {5, 3}, {7, 7},
      {8, 8}, {16, 16}, {17, 13}, {31, 31}, {100, 7}};
  for (const auto& [bd, ct] : kCombos) {
    for (const auto& [w, h] : sizes) {
      CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
      const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                      /*all_none=*/true);
      const auto layout = compute_scanline_layout(e.header);
      REQUIRE(layout.has_value());
      const PassReconstructionOutcome out =
          reconstruct_image(e.header, *layout, e.filtered);
      REQUIRE(out.success);
      REQUIRE(out.interlace);
      REQUIRE(out.passes.size() == 7);
      REQUIRE(out.target == e.raw);

      // Pass artifacts: byte counts match pass geometry; empty passes empty.
      // total_bytes == unfiltered data bytes + one filter byte per row.
      std::uint64_t placed = 0;
      std::uint64_t filter_bytes = 0;
      for (std::size_t p = 0; p < 7; ++p) {
        const auto& pass = layout->passes[p];
        const auto& art = out.passes[p];
        REQUIRE(art.pass_index == p);
        REQUIRE(art.rows.size() ==
                static_cast<std::size_t>(pass.height * pass.row_bytes));
        placed += static_cast<std::uint64_t>(art.rows.size());
        filter_bytes += pass.height;  // one filter byte per unfiltered row
      }
      REQUIRE(placed + filter_bytes == layout->total_bytes.value_or(0));
    }
  }
}

TEST_CASE("Production Adam7 geometry matches independent spec constants",
          "[png-reconstruction][wp303]") {
  for (std::uint32_t w : {1u, 2u, 3u, 8u, 16u, 17u, 31u, 100u}) {
    for (std::uint32_t h : {1u, 2u, 5u, 8u, 13u, 100u}) {
      CAPTURE(w, h);
      const auto layout = compute_scanline_layout(ImageHeader{w, h, 8, 6, true});
      REQUIRE(layout.has_value());
      const auto g = test_pass_geometry(w, h);
      for (std::size_t p = 0; p < 7; ++p) {
        CAPTURE(p);
        REQUIRE(layout->passes[p].width == g[p].w);
        REQUIRE(layout->passes[p].height == g[p].h);
        REQUIRE(layout->passes[p].x_start == g[p].xs);
        REQUIRE(layout->passes[p].y_start == g[p].ys);
        REQUIRE(layout->passes[p].x_step == g[p].xstep);
        REQUIRE(layout->passes[p].y_step == g[p].ystep);
      }
      REQUIRE(layout->total_pixels().has_value());
      REQUIRE(*layout->total_pixels() == w * h);
    }
  }
}

// ---------------------------------------------------------------------------
// B: non-interlaced reconstruction with rotating filters (unfiltering through
// the reconstruct entry point, including the degenerate placement path).
// ---------------------------------------------------------------------------

TEST_CASE("Non-interlaced reconstruction round-trips with all filters",
          "[png-reconstruction][wp303]") {
  const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>> cases = {
      {8, 8, 8, 6}, {8, 8, 1, 0}, {8, 8, 2, 3}, {8, 8, 4, 0},
      {16, 16, 16, 2}, {7, 9, 8, 4}, {13, 5, 8, 0}, {5, 5, 16, 6}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/false,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);
    REQUIRE_FALSE(out.interlace);
    REQUIRE(out.passes.size() == 1);
    REQUIRE(out.target == e.raw);
    REQUIRE(out.passes[0].rows == e.raw);
  }
}

// ---------------------------------------------------------------------------
// C: interlaced reconstruction with all five filters, verified against the
// libpng raw oracle.
// ---------------------------------------------------------------------------

TEST_CASE("Interlaced reconstruction matches libpng with rotating filters",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>> cases = {
      {8, 8, 8, 6}, {16, 12, 8, 2}, {10, 10, 16, 0}, {7, 9, 16, 6},
      {20, 20, 8, 4}, {33, 17, 8, 0}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.w == w);
    REQUIRE(oracle.h == h);
    REQUIRE(oracle.bd == bd);
    REQUIRE(oracle.ct == ct);
    REQUIRE(oracle.interlace);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// D: interlaced sub-byte depths (gray + palette), verified against libpng.
// ---------------------------------------------------------------------------

TEST_CASE("Interlaced sub-byte reconstruction matches libpng",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>> cases = {
      {8, 8, 1, 0}, {8, 8, 2, 0}, {8, 8, 4, 0}, {13, 7, 1, 3},
      {5, 17, 2, 3}, {16, 16, 4, 3}, {9, 9, 4, 0}};
  for (const auto& [w, h, bd, ct] : cases) {
    CAPTURE(w, h, static_cast<unsigned>(bd), static_cast<unsigned>(ct));
    const EncodedPng e = encode_png(w, h, bd, ct, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// E: empty passes produce empty artifacts while the composite stays correct.
// ---------------------------------------------------------------------------

TEST_CASE("Empty Adam7 passes yield empty artifacts and a correct target",
          "[png-reconstruction][wp303][oracle]") {
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes = {
      {1, 1}, {2, 1}, {1, 2}, {5, 3}, {8, 8}, {16, 16}, {31, 1}};
  for (const auto& [w, h] : sizes) {
    CAPTURE(w, h);
    const EncodedPng e = encode_png(w, h, 8, 6, /*interlace=*/true,
                                    /*all_none=*/false);
    const auto layout = compute_scanline_layout(e.header);
    REQUIRE(layout.has_value());
    const PassReconstructionOutcome out =
        reconstruct_image(e.header, *layout, e.filtered);
    REQUIRE(out.success);

    // The set of empty passes reported by reconstruction must agree with the
    // independent geometry, and empty passes must yield empty artifacts.
    const auto geom = test_pass_geometry(w, h);
    bool saw_empty = false;
    bool expected_empty = false;
    for (std::size_t p = 0; p < 7; ++p) {
      expected_empty = expected_empty || (geom[p].w == 0 || geom[p].h == 0);
      if (layout->passes[p].height == 0) {
        saw_empty = true;
        REQUIRE(out.passes[p].rows.empty());
      } else {
        REQUIRE_FALSE(out.passes[p].rows.empty());
      }
    }
    REQUIRE(saw_empty == expected_empty);

    const RawOracle oracle = raw_decode(e.png_bytes);
    REQUIRE(oracle.ok);
    REQUIRE(oracle.rows == out.target);
  }
}

// ---------------------------------------------------------------------------
// F: hostile / inconsistent input fails cleanly with a stable error.
// ---------------------------------------------------------------------------

TEST_CASE("Truncated or oversized filtered buffers are rejected",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/true,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());

  auto truncated = e.filtered;
  truncated.pop_back();
  const auto out_short =
      reconstruct_image(e.header, *layout, truncated);
  REQUIRE_FALSE(out_short.success);
  REQUIRE_FALSE(out_short.error.empty());
  REQUIRE(out_short.target.empty());

  auto oversized = e.filtered;
  oversized.push_back(std::byte{0});
  const auto out_long =
      reconstruct_image(e.header, *layout, oversized);
  REQUIRE_FALSE(out_long.success);
}

TEST_CASE("Header/layout mismatches and invalid headers are rejected",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/false,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());

  // Interlace flag disagreement.
  ImageHeader wrong_interlace = e.header;
  wrong_interlace.interlace = true;
  const auto out1 =
      reconstruct_image(wrong_interlace, *layout, e.filtered);
  REQUIRE_FALSE(out1.success);
  REQUIRE_FALSE(out1.error.empty());

  // Invalid bit depth (3 is not a PNG bit depth).
  ImageHeader bad_depth = e.header;
  bad_depth.bit_depth = 3;
  const auto out2 = reconstruct_image(bad_depth, *layout, e.filtered);
  REQUIRE_FALSE(out2.success);
  REQUIRE_FALSE(out2.error.empty());

  // Zero dimensions.
  ImageHeader zero = e.header;
  zero.width = 0;
  const auto out3 = reconstruct_image(zero, *layout, e.filtered);
  REQUIRE_FALSE(out3.success);
}

TEST_CASE("An invalid filter byte aborts reconstruction",
          "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, /*interlace=*/true,
                                  /*all_none=*/true);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());
  auto corrupted = e.filtered;
  corrupted[0] = std::byte{0xFF};  // first row's filter byte
  const auto out = reconstruct_image(e.header, *layout, corrupted);
  REQUIRE_FALSE(out.success);
  REQUIRE_FALSE(out.error.empty());
  // The no-partial-result contract holds even for mid-stream failures.
  REQUIRE(out.target.empty());
  REQUIRE(out.passes.empty());
}

// ---------------------------------------------------------------------------
// G: deterministic output.
// ---------------------------------------------------------------------------

TEST_CASE("Reconstruction is deterministic", "[png-reconstruction][wp303]") {
  const EncodedPng e = encode_png(17, 13, 8, 6, /*interlace=*/true,
                                  /*all_none=*/false);
  const auto layout = compute_scanline_layout(e.header);
  REQUIRE(layout.has_value());
  const PassReconstructionOutcome a =
      reconstruct_image(e.header, *layout, e.filtered);
  const PassReconstructionOutcome b =
      reconstruct_image(e.header, *layout, e.filtered);
  REQUIRE(a.success);
  REQUIRE(b.success);
  REQUIRE(a.target == b.target);
  REQUIRE(a.passes == b.passes);
}
