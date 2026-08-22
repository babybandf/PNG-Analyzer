#include <pnga/analysis-engine/stage_viewport.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using pnga::analysis_engine::StageSet;
using pnga::analysis_engine::StageViewportProvider;
using pnga::analysis_engine::ViewportRequest;
using pnga::analysis_engine::ViewportStatus;

namespace {

std::shared_ptr<StageSet> sample_stage() {
  auto stages = std::make_shared<StageSet>();
  stages->success = true;
  stages->native.width = 4;
  stages->native.height = 3;
  stages->native.channels = 2;
  stages->native.samples = {
      0, 1, 2, 3, 4, 5, 6, 7,
      8, 9, 10, 11, 12, 13, 14, 15,
      16, 17, 18, 19, 20, 21, 22, 23,
  };
  return stages;
}

}  // namespace

TEST_CASE("stage viewport returns bounded native sample window") {
  StageViewportProvider provider(sample_stage());
  ViewportRequest request;
  request.x = 1;
  request.y = 1;
  request.width = 2;
  request.height = 2;

  const auto view = provider.query(request);
  REQUIRE(view->status == ViewportStatus::kReady);
  REQUIRE(view->channels == 2);
  REQUIRE(view->samples == std::vector<std::uint16_t>{10, 11, 12, 13,
                                                        18, 19, 20, 21});
}

TEST_CASE("stage viewport caches identical request and rejects unsafe bounds") {
  StageViewportProvider provider(sample_stage());
  ViewportRequest request;
  request.width = 2;
  request.height = 1;
  const auto first = provider.query(request);
  const auto second = provider.query(request);
  REQUIRE(first == second);

  request.x = 3;
  request.width = 2;
  REQUIRE(provider.query(request)->status == ViewportStatus::kOutOfRange);

  request.x = 0;
  request.width = 2;
  request.max_samples = 1;
  REQUIRE(provider.query(request)->status == ViewportStatus::kBudgetExceeded);
}

TEST_CASE("stage viewport reports unavailable stages without decoding") {
  StageViewportProvider empty;
  ViewportRequest request;
  REQUIRE(empty.query(request)->status == ViewportStatus::kNoStage);

  StageViewportProvider provider(sample_stage());
  request.stage = pnga::trace_model::Stage::kFiltered;
  REQUIRE(provider.query(request)->status == ViewportStatus::kNotApplicable);
}
