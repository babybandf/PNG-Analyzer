#include <pnga/analysis-engine/trace_inspector_state.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("trace inspector lifecycle covers loading replay and cancel") {
  pnga::analysis_engine::TraceInspectorStateMachine machine;
  machine.replaceDocument(4);
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kEmpty);
  machine.beginLoading(4);
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kLoading);
  machine.markReplaying(4);
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kReplaying);
  REQUIRE(machine.cancel(4));
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kCancelled);
}

TEST_CASE("stale result cannot overwrite a newer document") {
  pnga::analysis_engine::TraceInspectorStateMachine machine;
  machine.replaceDocument(9);
  machine.beginLoading(9);
  pnga::analysis_engine::TraceQueryResult stale;
  stale.generation = 8;
  stale.status = pnga::analysis_engine::TraceQueryStatus::kReady;
  REQUIRE_FALSE(machine.publish(stale));
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kStaleGeneration);
  REQUIRE(machine.state().generation == 9);
  machine.replaceDocument(10);
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kEmpty);
  REQUIRE(machine.state().generation == 10);
}

TEST_CASE("trace inspector lifecycle maps partial and errors") {
  pnga::analysis_engine::TraceInspectorStateMachine machine;
  machine.replaceDocument(2);
  pnga::analysis_engine::TraceQueryResult partial;
  partial.generation = 2;
  partial.status = pnga::analysis_engine::TraceQueryStatus::kPartial;
  partial.error = "budget";
  REQUIRE(machine.publish(partial));
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kPartial);
  pnga::analysis_engine::TraceQueryResult error;
  error.generation = 2;
  error.status = pnga::analysis_engine::TraceQueryStatus::kError;
  error.error = "malformed";
  REQUIRE(machine.publish(error));
  REQUIRE(machine.state().status ==
          pnga::analysis_engine::TraceInspectorLifecycle::kError);
}
