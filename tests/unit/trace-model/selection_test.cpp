// WP-200 Selection model tests: equality, merge idempotence, multiple spans,
// invalid coordinates, deterministic serialization round-trip and dedup.

#include <pnga/trace-model/selection.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using pnga::trace_model::BitSpan;
using pnga::trace_model::deserialize;
using pnga::trace_model::ImageCoordinate;
using pnga::trace_model::PackedSampleCoordinate;
using pnga::trace_model::Selection;
using pnga::trace_model::SemanticNode;
using pnga::trace_model::serialize;
using pnga::trace_model::Stage;
using pnga::trace_model::stage_from_text;
using pnga::trace_model::stage_text;
using pnga::trace_model::StreamSpan;

TEST_CASE("An empty selection serializes to an empty string and stays empty",
          "[trace-model][wp200]") {
  Selection s;
  REQUIRE(s.empty());
  const std::string text = serialize(s);
  REQUIRE(text.empty());
  auto parsed = deserialize(text);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->empty());
  REQUIRE(*parsed == s);
}

TEST_CASE("Selections with every dimension round-trip through serialization",
          "[trace-model][wp200]") {
  Selection s;
  s.node = 7;
  s.physical_spans = {BitSpan{8, 13}, BitSpan{29, 4}, BitSpan{64, 2, 3, true}};
  s.logical = StreamSpan{0, 100};
  s.image = ImageCoordinate{0, 1, 2, 3, 4, 0};
  s.stage = Stage::kFiltered;

  const std::string text = serialize(s);
  auto parsed = deserialize(text);
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == s);
}

TEST_CASE("Whole-pixel coordinates remain distinct from channel zero",
          "[trace-model][wp5u1]") {
  Selection pixel;
  pixel.image = ImageCoordinate{0, 0, 4, 3, 2};
  REQUIRE(pixel.image->channel == std::nullopt);

  Selection channel = pixel;
  channel.image->channel = 0;
  REQUIRE(channel.image->channel.has_value());
  REQUIRE(pixel != channel);

  const auto parsed = deserialize(serialize(pixel));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->image.has_value());
  REQUIRE(parsed->image->channel == std::nullopt);
  REQUIRE(*parsed == pixel);
}

TEST_CASE("Sample byte and packed sample coordinates round-trip",
          "[trace-model][wp5u1]") {
  Selection sixteen;
  sixteen.image = ImageCoordinate{0, 0, 0, 1, 1, 2};
  sixteen.image->sample_byte = 1;
  auto parsed = deserialize(serialize(sixteen));
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == sixteen);

  Selection packed;
  packed.image = ImageCoordinate{0, 1, 2, 8, 8, 0};
  packed.image->packed_sample = PackedSampleCoordinate{2, 2};
  parsed = deserialize(serialize(packed));
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == packed);
}

TEST_CASE("Image coordinate validation rejects ambiguous sample addresses",
          "[trace-model][wp5u1]") {
  ImageCoordinate bad_pass{0, 8, 0, 0, 0};
  REQUIRE_FALSE(bad_pass.valid());

  ImageCoordinate bad_packed{0, 0, 0, 0, 0, 0};
  bad_packed.packed_sample = PackedSampleCoordinate{7, 2};
  REQUIRE_FALSE(bad_packed.valid());

  bad_packed.packed_sample = PackedSampleCoordinate{0, 3};
  REQUIRE_FALSE(bad_packed.valid());

  ImageCoordinate conflicting{0, 0, 0, 0, 0, 0};
  conflicting.sample_byte = 0;
  conflicting.packed_sample = PackedSampleCoordinate{0, 1};
  REQUIRE_FALSE(conflicting.valid());
}

TEST_CASE("Legacy six-field image serialization remains readable",
          "[trace-model][wp5u1]") {
  const auto parsed = deserialize("image:0,0,2,3,4,0");
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->image.has_value());
  REQUIRE(parsed->image->channel == std::optional<std::uint64_t>{0});
}

TEST_CASE("Multiple physical spans are preserved", "[trace-model][wp200]") {
  Selection s;
  s.node = 1;
  s.physical_spans = {BitSpan{0, 4}, BitSpan{16, 8}, BitSpan{100, 12}};
  const auto parsed = deserialize(serialize(s));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->physical_spans.size() == 3);
  REQUIRE(parsed->physical_spans[1] == BitSpan{16, 8});
}

TEST_CASE("Large and invalid-looking coordinates round-trip deterministically",
          "[trace-model][wp200]") {
  Selection s;
  s.image = ImageCoordinate{
      (std::uint64_t{1} << 48) + 5,  // very large frame number
      0xFFFFFFFFFFFFFFFFu,           // huge pass value
      0, 0, 0, 7};
  const auto parsed = deserialize(serialize(s));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->image.has_value());
  REQUIRE(*parsed->image == *s.image);
}

TEST_CASE("Equality distinguishes every dimension", "[trace-model][wp200]") {
  Selection a;
  a.node = 1;
  a.physical_spans = {BitSpan{0, 4}};

  Selection b = a;
  REQUIRE(a == b);

  b.node = 2;
  REQUIRE_FALSE(a == b);
  b = a;
  b.physical_spans = {BitSpan{1, 4}};
  REQUIRE_FALSE(a == b);
  b = a;
  b.logical = StreamSpan{0, 4};
  REQUIRE_FALSE(a == b);
  b = a;
  b.image = ImageCoordinate{};
  REQUIRE_FALSE(a == b);
  b = a;
  b.stage = Stage::kNative;
  REQUIRE_FALSE(a == b);
}

TEST_CASE("Merging a selection with itself is idempotent", "[trace-model][wp200]") {
  Selection s;
  s.node = 5;
  s.physical_spans = {BitSpan{8, 13}};
  s.logical = StreamSpan{2, 11};
  s.stage = Stage::kDelivered;

  const Selection merged = s.merged_with(s);
  REQUIRE(merged == s);
}

TEST_CASE("Merging disjoint dimensions combines them without losing data",
          "[trace-model][wp200]") {
  Selection a;
  a.node = 1;
  a.logical = StreamSpan{0, 64};

  Selection b;
  b.physical_spans = {BitSpan{16, 8}};
  b.image = ImageCoordinate{0, 0, 5, 2, 3, 0};

  const Selection merged = a.merged_with(b);
  REQUIRE(merged.node == a.node);
  REQUIRE(merged.logical == a.logical);
  REQUIRE(merged.physical_spans == b.physical_spans);
  REQUIRE(merged.image == b.image);
  REQUIRE(merged == b.merged_with(a));  // commutative for disjoint fields
}

TEST_CASE("Merging a selection with itself does not grow spans (dedup)",
          "[trace-model][wp200]") {
  Selection s;
  s.physical_spans = {BitSpan{0, 4}, BitSpan{8, 4}};
  const Selection merged = s.merged_with(s);
  REQUIRE(merged.physical_spans.size() == 2);
  REQUIRE(merged == s);
}

TEST_CASE("Newer field values win on conflict", "[trace-model][wp200]") {
  Selection old_sel;
  old_sel.node = 1;
  old_sel.logical = StreamSpan{0, 4};

  Selection new_sel;
  new_sel.node = 2;
  new_sel.logical = StreamSpan{0, 8};

  const Selection merged = old_sel.merged_with(new_sel);
  REQUIRE(merged.node == 2);
  REQUIRE(merged.logical == StreamSpan{0, 8});
}

TEST_CASE("Stage text mapping is stable and reversible", "[trace-model][wp200]") {
  REQUIRE(std::string(stage_text(Stage::kFile)) == "file");
  REQUIRE(std::string(stage_text(Stage::kChunk)) == "chunk");
  REQUIRE(std::string(stage_text(Stage::kFiltered)) == "filtered");
  REQUIRE(std::string(stage_text(Stage::kDelivered)) == "delivered");
  REQUIRE(std::string(stage_text(Stage::kUnknown)) == "unknown");

  Stage s = Stage::kUnknown;
  REQUIRE(stage_from_text("native", s));
  REQUIRE(s == Stage::kNative);
  REQUIRE_FALSE(stage_from_text("bogus", s));
}

TEST_CASE("Malformed serialized input returns nullopt", "[trace-model][wp200]") {
  REQUIRE_FALSE(deserialize("node:").has_value());             // empty value
  REQUIRE_FALSE(deserialize("node:abc").has_value());          // non-numeric
  REQUIRE_FALSE(deserialize("node:1;bogus:2").has_value());    // unknown key
  REQUIRE_FALSE(deserialize("image:1,2").has_value());         // wrong arity
  REQUIRE_FALSE(deserialize("physical:1").has_value());        // wrong arity
  REQUIRE_FALSE(deserialize("image:0,0,0,0,0;packed_sample:7,2")
                    .has_value());                              // crosses byte
  REQUIRE_FALSE(deserialize("image:0,0,0,0,0;channel:0;packed_sample:0,3")
                    .has_value());                              // illegal packed width
  REQUIRE_FALSE(deserialize("stage:").has_value());            // empty stage
  REQUIRE_FALSE(deserialize("nocolon").has_value());           // no key/value
}

TEST_CASE("SemanticNode compares by value", "[trace-model][wp200]") {
  SemanticNode a;
  a.id = 3;
  a.stage = Stage::kChunk;
  a.physical_spans = {BitSpan{8, 13}};
  SemanticNode b = a;
  REQUIRE(a == b);
  b.id = 4;
  REQUIRE_FALSE(a == b);
}
