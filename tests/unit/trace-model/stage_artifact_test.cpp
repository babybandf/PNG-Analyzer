#include <pnga/trace-model/stage_artifact.h>

#include <catch2/catch_test_macros.hpp>

using pnga::trace_model::AnimationFrame;
using pnga::trace_model::ArtifactKey;
using pnga::trace_model::Stage;

TEST_CASE("Artifact keys distinguish static image from animation frame zero",
          "[trace-model][wp699]") {
  ArtifactKey static_key;
  static_key.stage = Stage::kDelivered;

  ArtifactKey frame_key = static_key;
  frame_key.identity = AnimationFrame{0};

  REQUIRE(static_key != frame_key);
  REQUIRE((static_key < frame_key) != (frame_key < static_key));
}

TEST_CASE("Artifact key ordering is identity then stage then row range",
          "[trace-model][wp699]") {
  ArtifactKey static_delivered;
  static_delivered.stage = Stage::kDelivered;
  static_delivered.row_begin = 10;
  static_delivered.row_end = 20;

  ArtifactKey static_frame_output = static_delivered;
  static_frame_output.stage = Stage::kFrameOutput;

  ArtifactKey frame_delivered = static_delivered;
  frame_delivered.identity = AnimationFrame{1};

  REQUIRE(static_delivered < static_frame_output);
  REQUIRE(static_delivered < frame_delivered);
}
