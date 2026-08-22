// WP-5U3B bounded native-sample viewport provider.

#include "pnga/analysis-engine/stage_viewport.h"

#include <limits>

namespace pnga::analysis_engine {

namespace {

bool checked_mul(std::uint64_t lhs, std::uint64_t rhs,
                 std::uint64_t& out) noexcept {
  if (rhs != 0 && lhs > std::numeric_limits<std::uint64_t>::max() / rhs) {
    return false;
  }
  out = lhs * rhs;
  return true;
}

bool checked_add(std::uint64_t lhs, std::uint64_t rhs,
                 std::uint64_t& out) noexcept {
  if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

std::shared_ptr<const StageViewport> result(ViewportStatus status) {
  auto out = std::make_shared<StageViewport>();
  out->status = status;
  return out;
}

}  // namespace

const char* viewport_status_text(ViewportStatus status) noexcept {
  switch (status) {
    case ViewportStatus::kReady:
      return "ready";
    case ViewportStatus::kNoStage:
      return "no stage";
    case ViewportStatus::kNotApplicable:
      return "not applicable";
    case ViewportStatus::kOutOfRange:
      return "out of range";
    case ViewportStatus::kBudgetExceeded:
      return "budget exceeded";
    case ViewportStatus::kError:
      return "error";
  }
  return "error";
}

StageViewportProvider::StageViewportProvider(
    std::shared_ptr<const StageSet> stages)
    : stages_(std::move(stages)) {}

void StageViewportProvider::setStageSet(
    std::shared_ptr<const StageSet> stages) {
  stages_ = std::move(stages);
  cached_request_.reset();
  cached_result_.reset();
  ++cache_generation_;
}

void StageViewportProvider::clear() noexcept {
  stages_.reset();
  cached_request_.reset();
  cached_result_.reset();
  ++cache_generation_;
}

std::shared_ptr<const StageViewport> StageViewportProvider::query(
    const ViewportRequest& request) {
  if (cached_request_.has_value() && cached_result_ != nullptr &&
      *cached_request_ == request) {
    return cached_result_;
  }

  std::shared_ptr<const StageViewport> out;
  if (stages_ == nullptr || !stages_->success) {
    out = result(ViewportStatus::kNoStage);
  } else if (request.stage != pnga::trace_model::Stage::kNative) {
    out = result(ViewportStatus::kNotApplicable);
  } else if (request.width == 0 || request.height == 0 ||
             request.max_samples == 0) {
    out = result(ViewportStatus::kOutOfRange);
  } else {
    const auto& native = stages_->native;
    std::uint64_t sample_count = 0;
    std::uint64_t row_samples = 0;
    std::uint64_t end_x = 0;
    std::uint64_t end_y = 0;
    if (!checked_mul(request.width, native.channels, row_samples) ||
        !checked_mul(row_samples, request.height, sample_count) ||
        !checked_add(request.x, request.width, end_x) ||
        !checked_add(request.y, request.height, end_y)) {
      out = result(ViewportStatus::kOutOfRange);
    } else if (native.channels == 0 || request.x >= native.width ||
               request.y >= native.height || end_x > native.width ||
               end_y > native.height) {
      out = result(ViewportStatus::kOutOfRange);
    } else if (sample_count > request.max_samples ||
               sample_count > static_cast<std::uint64_t>(
                                   std::numeric_limits<std::size_t>::max())) {
      out = result(ViewportStatus::kBudgetExceeded);
    } else {
      std::uint64_t base_pixel = 0;
      std::uint64_t source_row = 0;
      std::uint64_t source_end = 0;
      if (!checked_mul(request.y, native.width, source_row) ||
          !checked_add(source_row, request.x, base_pixel) ||
          !checked_mul(end_y - 1, native.width, source_end) ||
          !checked_add(source_end, end_x - request.x, source_end) ||
          !checked_mul(source_end, native.channels, source_end) ||
          source_end > native.samples.size()) {
        out = result(ViewportStatus::kError);
      } else {
        auto ready = std::make_shared<StageViewport>();
        ready->status = ViewportStatus::kReady;
        ready->x = request.x;
        ready->y = request.y;
        ready->width = request.width;
        ready->height = request.height;
        ready->channels = native.channels;
        ready->cache_generation = cache_generation_;
        ready->samples.reserve(static_cast<std::size_t>(sample_count));
        for (std::uint64_t row = 0; row < request.height; ++row) {
          std::uint64_t pixel = 0;
          std::uint64_t row_base = 0;
          if (!checked_add(request.y, row, row_base) ||
              !checked_mul(row_base, native.width, row_base) ||
              !checked_add(row_base, request.x, pixel) ||
              !checked_mul(pixel, native.channels, pixel)) {
            out = result(ViewportStatus::kError);
            break;
          }
          const auto begin = static_cast<std::size_t>(pixel);
          const auto length = static_cast<std::size_t>(row_samples);
          ready->samples.insert(ready->samples.end(),
                                native.samples.begin() + begin,
                                native.samples.begin() + begin + length);
        }
        if (out == nullptr) {
          out = std::move(ready);
        }
      }
    }
  }

  cached_request_ = request;
  cached_result_ = out;
  return out;
}

}  // namespace pnga::analysis_engine
