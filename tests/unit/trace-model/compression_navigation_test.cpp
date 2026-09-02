// WP-5U12B Task 1: typed Compression navigation values. The source unit,
// logical range variant, navigation target, Current mapping and Selection
// state are Qt-free value types; validity rules enforce non-empty ranges,
// non-zero serials and ordered non-overlapping physical file spans.

#include <pnga/trace-model/compression_navigation.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

using namespace pnga::trace_model;

namespace {

FileByteRange file_span(std::uint64_t begin, std::uint64_t length) {
  return FileByteRange{FileByteOffset{begin}, FileByteOffset{begin + length}};
}

CompressionNavigationTarget make_target(
    CompressionLogicalRange logical_range, std::vector<FileByteRange> spans,
    std::uint64_t serial = 1) {
  CompressionNavigationTarget target;
  target.generation = 7;
  target.request_serial = serial;
  target.source_unit = DocumentSourceUnit{};
  target.origin = CompressionNavigationOrigin::kBlocks;
  target.logical_range = std::move(logical_range);
  target.physical_spans = std::move(spans);
  return target;
}

}  // namespace

TEST_CASE("Document source units separate file and animation frames",
          "[wp5u12b][navigation]") {
  const DocumentSourceUnit file_default{};
  REQUIRE(file_default.kind == DocumentSourceUnitKind::kFile);
  REQUIRE(file_default.index == 0);
  REQUIRE(file_default.valid());

  DocumentSourceUnit file_nonzero;
  file_nonzero.kind = DocumentSourceUnitKind::kFile;
  file_nonzero.index = 3;
  REQUIRE_FALSE(file_nonzero.valid());

  DocumentSourceUnit frame_zero;
  frame_zero.kind = DocumentSourceUnitKind::kAnimationFrame;
  frame_zero.index = 0;
  REQUIRE(frame_zero.valid());

  DocumentSourceUnit frame_four;
  frame_four.kind = DocumentSourceUnitKind::kAnimationFrame;
  frame_four.index = 4;
  REQUIRE(frame_four.valid());

  REQUIRE(file_default == DocumentSourceUnit{});
  REQUIRE(file_default != file_nonzero);
  REQUIRE(frame_zero != frame_four);
  REQUIRE(frame_zero != file_default);
}

TEST_CASE("All five logical range variants validate with exact spans",
          "[wp5u12b][navigation]") {
  // File navigation selects its exact file range.
  REQUIRE(make_target(FileByteRange{FileByteOffset{0}, FileByteOffset{8}},
                      {file_span(0, 8)})
              .valid());

  // A zlib byte range crossing two IDAT chunks carries both ordered spans.
  const CompressionNavigationTarget zlib_cross_idat =
      make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{10}},
                  {file_span(33, 4), file_span(45, 6)});
  REQUIRE(zlib_cross_idat.valid());
  REQUIRE(zlib_cross_idat.physical_spans.size() == 2);
  REQUIRE(zlib_cross_idat.physical_spans[0] == file_span(33, 4));
  REQUIRE(zlib_cross_idat.physical_spans[1] == file_span(45, 6));

  // zlib bit ranges map onto the same physical file spans.
  REQUIRE(make_target(ZlibBitRange{ZlibBitOffset{16}, ZlibBitOffset{24}},
                      {file_span(33, 1)})
              .valid());

  // DEFLATE payload bits also carry every mapped physical span.
  REQUIRE(make_target(
              DeflateBitRange{DeflateBitOffset{0}, DeflateBitOffset{8}},
              {file_span(33, 4), file_span(45, 6)})
              .valid());

  // Inflated output ranges require no physical spans at all.
  REQUIRE(make_target(InflatedByteRange{InflatedByteOffset{0},
                                        InflatedByteOffset{2}},
                      {})
              .valid());

  REQUIRE_FALSE(make_target(InflatedByteRange{InflatedByteOffset{0},
                                              InflatedByteOffset{2}},
                            {file_span(33, 4)})
                    .valid());
  REQUIRE_FALSE(make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{10}},
                            {})
                    .valid());
}

TEST_CASE("Every logical range and physical span must be non-empty",
          "[wp5u12b][navigation]") {
  REQUIRE_FALSE(make_target(FileByteRange{}, {}).valid());
  REQUIRE_FALSE(make_target(ZlibByteRange{}, {}).valid());
  REQUIRE_FALSE(make_target(ZlibBitRange{}, {}).valid());
  REQUIRE_FALSE(make_target(DeflateBitRange{}, {}).valid());
  REQUIRE_FALSE(make_target(InflatedByteRange{}, {}).valid());

  // A reversed range is invalid, not merely empty.
  REQUIRE_FALSE(
      make_target(FileByteRange{FileByteOffset{8}, FileByteOffset{0}}, {})
          .valid());

  REQUIRE_FALSE(make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{4}},
                            {file_span(33, 0)})
                    .valid());
}

TEST_CASE("Physical spans stay ordered, non-overlapping and complete",
          "[wp5u12b][navigation]") {
  // Overlapping spans are rejected in caller order.
  REQUIRE_FALSE(make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{8}},
                            {file_span(100, 10), file_span(105, 10)})
                    .valid());
  // Disordered spans are rejected; the caller's order is preserved, never
  // sorted or merged.
  REQUIRE_FALSE(make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{8}},
                            {file_span(105, 10), file_span(100, 5)})
                    .valid());
  // Adjacent half-open spans are not overlapping.
  REQUIRE(make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{10}},
                      {file_span(100, 5), file_span(105, 5)})
              .valid());

  // The multi-span value survives a copy round trip unchanged.
  const CompressionNavigationTarget target =
      make_target(ZlibByteRange{ZlibByteOffset{0}, ZlibByteOffset{10}},
                  {file_span(33, 4), file_span(45, 6)});
  const CompressionNavigationTarget round_trip = target;
  REQUIRE(round_trip == target);
  REQUIRE(round_trip.physical_spans == target.physical_spans);
}

TEST_CASE("Navigation serials must be non-zero", "[wp5u12b][navigation]") {
  const auto valid_range = CompressionLogicalRange{
      InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{2}}};
  REQUIRE_FALSE(make_target(valid_range, {}, 0).valid());
  REQUIRE(make_target(valid_range, {}, 1).valid());
}

TEST_CASE("Current mapping, manual target and state compare by value",
          "[wp5u12b][navigation]") {
  const CompressionCurrentMapping mapping;
  CompressionCurrentMapping other = mapping;
  other.block_index = 3;
  REQUIRE(mapping != other);

  const CompressionNavigationTarget manual = make_target(
      CompressionLogicalRange{
          InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{2}}},
      {}, 1);
  CompressionNavigationTarget other_manual = manual;
  other_manual.symbol = std::uint16_t{12};
  REQUIRE(manual != other_manual);
  REQUIRE(other_manual.symbol == std::optional<std::uint16_t>{12});

  // Current mapping and Manual Selection coexist in one state.
  const CompressionSelectionState state{7, mapping, manual};
  REQUIRE(state.generation == 7);
  REQUIRE(state.current.has_value());
  REQUIRE(state.manual.has_value());
  REQUIRE(state == CompressionSelectionState{7, mapping, manual});
  REQUIRE(state != CompressionSelectionState{7, mapping, other_manual});
  REQUIRE(state != CompressionSelectionState{8, mapping, manual});
  REQUIRE(state != CompressionSelectionState{7, other, manual});
  REQUIRE(state !=
          CompressionSelectionState{7, mapping, std::nullopt});
}

TEST_CASE("Generation 7 to 8 reset values clear current and manual",
          "[wp5u12b][navigation]") {
  const CompressionCurrentMapping mapping;
  const CompressionNavigationTarget manual = make_target(
      CompressionLogicalRange{
          InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{2}}},
      {}, 1);
  const CompressionSelectionState before{7, mapping, manual};
  REQUIRE(before.current.has_value());
  REQUIRE(before.manual.has_value());

  const CompressionSelectionState after_reset{8, std::nullopt, std::nullopt};
  REQUIRE(after_reset.generation == 8);
  REQUIRE_FALSE(after_reset.current.has_value());
  REQUIRE_FALSE(after_reset.manual.has_value());
  REQUIRE(after_reset != before);
}
