// WP-5U12 P0-A: compression offset domains are explicit and half-open.

#include <pnga/trace-model/offset_range.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>

using pnga::trace_model::DeflateBitOffset;
using pnga::trace_model::DeflateBitRange;
using pnga::trace_model::FileByteRange;
using pnga::trace_model::InflatedByteOffset;
using pnga::trace_model::InflatedByteRange;
using pnga::trace_model::ZlibBitOffset;
using pnga::trace_model::ZlibBitRange;
using pnga::trace_model::make_range;

TEST_CASE("Compression ranges keep their coordinate domains", "[wp5u12]") {
  static_assert(!std::is_same_v<ZlibBitOffset, DeflateBitOffset>);
  static_assert(!std::is_same_v<ZlibBitRange, DeflateBitRange>);
  static_assert(!std::is_same_v<FileByteRange, InflatedByteRange>);

  const ZlibBitRange zlib{ZlibBitOffset{16}, ZlibBitOffset{539}};
  const DeflateBitRange deflate{DeflateBitOffset{0}, DeflateBitOffset{523}};
  const InflatedByteRange output{InflatedByteOffset{0},
                                 InflatedByteOffset{3104}};

  REQUIRE(zlib.valid());
  REQUIRE(zlib.contains(ZlibBitOffset{16}));
  REQUIRE_FALSE(zlib.contains(ZlibBitOffset{539}));
  REQUIRE(deflate.valid());
  REQUIRE(output.valid());
  REQUIRE_FALSE(zlib.overlaps(ZlibBitRange{ZlibBitOffset{539},
                                            ZlibBitOffset{600}}));
}

TEST_CASE("Compression range construction checks end overflow", "[wp5u12]") {
  const auto valid = make_range(ZlibBitOffset{10}, 5);
  REQUIRE(valid.has_value());
  REQUIRE(valid->begin == ZlibBitOffset{10});
  REQUIRE(valid->end == ZlibBitOffset{15});

  const auto overflow = make_range(
      ZlibBitOffset{std::numeric_limits<std::uint64_t>::max()},
      std::uint64_t{1});
  REQUIRE_FALSE(overflow.has_value());
}

TEST_CASE("Compression ranges expose half-open overlap semantics", "[wp5u12]") {
  const DeflateBitRange first{DeflateBitOffset{8}, DeflateBitOffset{16}};
  const DeflateBitRange adjacent{DeflateBitOffset{16}, DeflateBitOffset{24}};
  const DeflateBitRange intersecting{DeflateBitOffset{15},
                                     DeflateBitOffset{20}};

  REQUIRE_FALSE(first.overlaps(adjacent));
  REQUIRE(first.overlaps(intersecting));
  REQUIRE(first.end - first.begin == 8);
}
