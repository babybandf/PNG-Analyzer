// WP-APNG-INSPECT T01: inspection ticket identity and publication scope
// rules (contract C1). Target-scoped publications match key and target
// epoch; pixel-scoped publications additionally match stage and selection
// serial.

#include <pnga/trace-model/inspection_context.h>

#include <catch2/catch_test_macros.hpp>

using namespace pnga::trace_model;

TEST_CASE("Target scope accepts by key and epoch, ignoring serial",
          "[apng-inspect]") {
  const InspectionTicket a{{7, AnimationFrame{0}}, Stage::kFrameOutput, 1, 3};

  auto b = a;
  b.target_epoch = 2;
  REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kTarget));

  b = a;
  ++b.selection_serial;
  REQUIRE(accepts_publication(b, a, PublicationScope::kTarget));
  REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kPixel));

  b = a;
  b.key.identity = StaticImage{};
  REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kTarget));
}

TEST_CASE("Pixel scope additionally requires the same stage",
          "[apng-inspect]") {
  const InspectionTicket a{{7, AnimationFrame{0}}, Stage::kPreBlend, 1, 3};

  auto b = a;
  b.stage = Stage::kPostBlend;
  REQUIRE(accepts_publication(b, a, PublicationScope::kTarget));
  REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kPixel));

  b = a;
  ++b.selection_serial;
  REQUIRE(accepts_publication(b, a, PublicationScope::kTarget));
  REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kPixel));
}

TEST_CASE("Identical tickets accept under both scopes", "[apng-inspect]") {
  const InspectionTicket a{{7, AnimationFrame{1}}, Stage::kFrameOutput, 4, 9};
  REQUIRE(accepts_publication(a, a, PublicationScope::kTarget));
  REQUIRE(accepts_publication(a, a, PublicationScope::kPixel));
}
