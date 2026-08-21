// WP-302 reverse filter tests: golden vectors per filter type, the Paeth tie
// rule, modulo-256 wraparound, invalid filter rejection and randomized
// forward->unfilter round-trips across bpp values.

#include <pnga/png-reconstruction/reverse_filter.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

using pnga::png_reconstruction::FilterTraceEvent;
using pnga::png_reconstruction::FilterType;
using pnga::png_reconstruction::is_valid_filter_type;
using pnga::png_reconstruction::paeth_predictor;
using pnga::png_reconstruction::reconstruct_byte;
using pnga::png_reconstruction::unfilter_scanline;
using pnga::png_reconstruction::unfilter_scanline_traced;

namespace {

std::byte B(unsigned int v) { return static_cast<std::byte>(v & 0xFF); }

std::vector<std::byte> bytes_of(std::initializer_list<unsigned int> list) {
  std::vector<std::byte> out;
  for (auto v : list) {
    out.push_back(B(v));
  }
  return out;
}

// Independent test oracle for the PNG Paeth predictor. Do not call the
// production implementation here: the randomized round-trip must be able to
// detect a predictor defect in that implementation.
std::uint8_t reference_paeth(std::uint8_t a, std::uint8_t b,
                             std::uint8_t c) {
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

std::uint8_t reference_predictor(FilterType type, std::uint8_t a,
                                 std::uint8_t b, std::uint8_t c) {
  switch (type) {
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
      return reference_paeth(a, b, c);
  }
  return 0;
}

// Forward filter (test-only): maps an unfiltered row to its filtered form.
std::vector<std::byte> forward_filter(FilterType type,
                                      const std::vector<std::byte>& row,
                                      const std::vector<std::byte>& prev,
                                      std::uint64_t bpp) {
  std::vector<std::byte> out(row.size());
  for (std::size_t i = 0; i < row.size(); ++i) {
    const std::uint8_t a = i >= bpp ? static_cast<std::uint8_t>(row[i - bpp]) : 0;
    const std::uint8_t b =
        i < prev.size() ? static_cast<std::uint8_t>(prev[i]) : 0;
    const std::uint8_t c =
        (i >= bpp && i < prev.size()) ? static_cast<std::uint8_t>(prev[i - bpp]) : 0;
    const std::uint8_t pred = reference_predictor(type, a, b, c);
    out[i] = static_cast<std::byte>(
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(row[i]) - pred));
  }
  return out;
}

}  // namespace

TEST_CASE("Paeth predictor follows the spec tie rule", "[png-recon][wp302]") {
  // Equal distances: a is preferred over b, b over c.
  REQUIRE(paeth_predictor(10, 10, 0) == 10);   // pa==pb -> a
  REQUIRE(paeth_predictor(10, 5, 0) == 10);    // pa < pb
  REQUIRE(paeth_predictor(5, 10, 0) == 10);    // pb < pa
  REQUIRE(paeth_predictor(0, 0, 10) == 0);     // pc large, pa==pb==0 -> a
  REQUIRE(paeth_predictor(1, 1, 0) == 1);      // a preferred on tie
  REQUIRE(paeth_predictor(0, 1, 0) == 1);      // pb=0, so b wins
  // Wraparound-heavy case from the spec discussion (values stay bytes).
  REQUIRE(paeth_predictor(200, 200, 0) == 200);
}

TEST_CASE("Reconstruct handles each filter type as golden", "[png-recon][wp302]") {
  // x=10 with neighbors a=5, b=7, c=3.
  REQUIRE(reconstruct_byte(FilterType::kNone, 10, 5, 7, 3) == 10);
  REQUIRE(reconstruct_byte(FilterType::kSub, 10, 5, 7, 3) == 15);
  REQUIRE(reconstruct_byte(FilterType::kUp, 10, 5, 7, 3) == 17);
  REQUIRE(reconstruct_byte(FilterType::kAverage, 10, 5, 7, 3) == 16);  // +6
  REQUIRE(reconstruct_byte(FilterType::kPaeth, 10, 5, 7, 3) == 17);    // +7
}

TEST_CASE("Modulo 256 wraparound is preserved", "[png-recon][wp302]") {
  REQUIRE(reconstruct_byte(FilterType::kSub, 200, 100, 0, 0) == 44);   // 300 % 256
  REQUIRE(reconstruct_byte(FilterType::kUp, 200, 0, 100, 0) == 44);
  REQUIRE(reconstruct_byte(FilterType::kAverage, 1, 0, 255, 0) == 128);  // 1+127
  REQUIRE(reconstruct_byte(FilterType::kPaeth, 250, 200, 200, 0) ==
          static_cast<std::uint8_t>(250 + 200));
}

TEST_CASE("Invalid filter types are rejected without touching the row",
          "[png-recon][wp302]") {
  REQUIRE_FALSE(is_valid_filter_type(5));
  std::vector<std::byte> row = bytes_of({1, 2, 3});
  const auto before = row;
  REQUIRE_FALSE(unfilter_scanline(static_cast<FilterType>(5), row.data(),
                                  row.size(), nullptr, 0, 1));
  REQUIRE(row == before);
}

TEST_CASE("First row uses zero neighbors (Up and Average)", "[png-recon][wp302]") {
  // First row, bpp=3: Up uses prev=0, Average uses b=0.
  std::vector<std::byte> row = bytes_of({5, 6, 7});
  REQUIRE(unfilter_scanline(FilterType::kUp, row.data(), row.size(), nullptr,
                            0, 3));
  REQUIRE(row == bytes_of({5, 6, 7}));

  row = bytes_of({5, 6, 7});
  REQUIRE(unfilter_scanline(FilterType::kAverage, row.data(), row.size(),
                            nullptr, 0, 3));
  REQUIRE(row == bytes_of({5, 6, 7}));  // a=0, b=0 -> predictor 0
}

TEST_CASE("First bpp bytes use a zero left neighbor (Sub)", "[png-recon][wp302]") {
  // bpp=3: byte 0 and 1 use a=0.
  std::vector<std::byte> row = bytes_of({10, 10, 10, 10, 10, 10});
  REQUIRE(unfilter_scanline(FilterType::kSub, row.data(), row.size(), nullptr,
                            0, 3));
  REQUIRE(row == bytes_of({10, 10, 10, 20, 20, 20}));
}

TEST_CASE("Round-trip forward then unfilter restores rows for all filters",
          "[png-recon][wp302]") {
  std::mt19937 rng(42);
  for (std::uint64_t bpp : {1u, 2u, 3u, 4u, 6u, 8u}) {
    for (int type = 0; type <= 4; ++type) {
      const FilterType ft = static_cast<FilterType>(type);
      for (int trial = 0; trial < 200; ++trial) {
        const std::size_t len = 16 + rng() % 32;
        std::vector<std::byte> row(len);
        std::vector<std::byte> prev(len);
        for (std::size_t i = 0; i < len; ++i) {
          row[i] = B(rng());
          prev[i] = B(rng());
        }
        auto filtered = forward_filter(ft, row, prev, bpp);
        REQUIRE(unfilter_scanline(ft, filtered.data(), filtered.size(),
                                  prev.data(), prev.size(), bpp));
        REQUIRE(filtered == row);
      }
    }
  }
}

TEST_CASE("Traced unfilter records raw, neighbors, predictor and recon",
          "[png-recon][wp302]") {
  // bpp=1, Sub, first row: row [10, 20, 30] unfilters to [10, 30, 60].
  std::vector<std::byte> row = bytes_of({10, 20, 30});
  std::vector<FilterTraceEvent> events;
  REQUIRE(unfilter_scanline_traced(FilterType::kSub, row.data(), row.size(),
                                   nullptr, 0, 1, events));
  REQUIRE(row == bytes_of({10, 30, 60}));
  REQUIRE(events.size() == 3);
  REQUIRE(events[1].index == 1);
  REQUIRE(events[1].raw == 20);
  REQUIRE(events[1].a == 10);  // left neighbor
  REQUIRE(events[1].b == 0);
  REQUIRE(events[1].predictor == 10);
  REQUIRE(events[1].recon == 30);
}

TEST_CASE("Golden example with a previous row (Average)", "[png-recon][wp302]") {
  // prev = [1, 2, 3], bpp=3, row = [9, 8, 7] filtered with Average.
  // Byte 0: pred = (0+1)/2 = 0 -> recon 9.
  // Byte 1: pred = (0+2)/2 = 1 -> recon 9.
  // Byte 2: pred = (0+3)/2 = 1 -> recon 8.
  std::vector<std::byte> row = bytes_of({9, 8, 7});
  const std::vector<std::byte> prev = bytes_of({1, 2, 3});
  REQUIRE(unfilter_scanline(FilterType::kAverage, row.data(), row.size(),
                            prev.data(), prev.size(), 3));
  REQUIRE(row == bytes_of({9, 9, 8}));
}
