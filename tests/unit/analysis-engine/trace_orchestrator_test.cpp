// WP-5T0B orchestration tests: bounded worker submission, cancellation,
// stale-generation suppression, source lifetime and ready result delivery.

#include <pnga/analysis-engine/trace_orchestrator.h>
#include <pnga/analysis-engine/trace_inspector_bundle.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "test_png_helpers.h"

using pnga::analysis_engine::TraceOrchestrationRequest;
using pnga::analysis_engine::TraceOrchestrator;
using pnga::analysis_engine::TraceQueryStatus;
using pnga::analysis_engine::TraceSubmitStatus;
using pnga::deflate_index::Adler32Status;
using pnga::io::MemoryByteSource;

namespace {

std::shared_ptr<const pnga::io::IByteSource> shared_source(
    const std::vector<std::byte>& bytes) {
  return std::make_shared<MemoryByteSource>(bytes);
}

}  // namespace

TEST_CASE("Fast index publishes complete blocks without any trace job",
          "[analysis-engine][wp5u12a]") {
  const auto encoded =
      pnga_test::encode_png(8, 8, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  TraceOrchestrator orchestrator(/*worker_count=*/1, /*budget=*/1u << 20);
  REQUIRE(orchestrator.open(shared_source(encoded.png_bytes), 1u << 20));
  REQUIRE(orchestrator.has_index());
  REQUIRE(orchestrator.queued_tasks() == 0);

  const auto fast = orchestrator.fast_index();
  REQUIRE(fast.status ==
          pnga::analysis_engine::FastCompressionIndexStatus::kReady);
  REQUIRE(fast.generation == orchestrator.document_generation());
  REQUIRE(fast.stream.deflate_data_begin ==
          pnga::trace_model::ZlibByteOffset{2});
  REQUIRE(fast.stream.wrapper.compression_method == 8);
  REQUIRE(fast.stream.wrapper.window_bits == 15);
  REQUIRE(fast.stream.wrapper.header_valid);
  REQUIRE_FALSE(fast.stream.wrapper.preset_dictionary);
  REQUIRE(fast.stream.adler.status == Adler32Status::kMatch);
  REQUIRE(fast.stream.adler.expected == fast.stream.adler.actual);
  REQUIRE_FALSE(fast.stream.stop_input.has_value());
  REQUIRE_FALSE(fast.stream.stop_output.has_value());
  REQUIRE_FALSE(fast.stream.idat_spans.empty());

  // The IDAT spans tile the whole logical stream without gaps.
  std::uint64_t span_bytes = 0;
  for (const auto& span : fast.stream.idat_spans) {
    span_bytes += span.logical_range.end.raw_value() -
                  span.logical_range.begin.raw_value();
  }
  REQUIRE(span_bytes == fast.stream.stream_range.end.raw_value());

  // The complete Block list is available with no submitted work.
  REQUIRE_FALSE(fast.blocks.empty());
  REQUIRE(fast.blocks.back().last);
  REQUIRE(orchestrator.queued_tasks() == 0);
}

TEST_CASE("Trace submit status text is stable", "[analysis-engine][wp5t0b]") {
  REQUIRE(std::string(pnga::analysis_engine::trace_submit_status_text(
              TraceSubmitStatus::kQueued)) == "queued");
  REQUIRE(std::string(pnga::analysis_engine::trace_submit_status_text(
              TraceSubmitStatus::kNotIndexed)) == "not indexed");
  REQUIRE(std::string(pnga::analysis_engine::trace_submit_status_text(
              TraceSubmitStatus::kStaleGeneration)) == "stale generation");
  REQUIRE(std::string(pnga::analysis_engine::trace_submit_status_text(
              TraceSubmitStatus::kRejected)) == "rejected");
}

TEST_CASE("Trace orchestrator delivers a bounded ready result",
          "[analysis-engine][wp5t0b]") {
  const auto encoded =
      pnga_test::encode_png(32, 16, 8, 6, /*interlace=*/false,
                            /*all_none=*/false);
  TraceOrchestrator orchestrator(/*worker_count=*/1, /*budget=*/1u << 20);
  std::mutex mutex;
  std::condition_variable cv;
  std::shared_ptr<pnga::analysis_engine::TraceQueryResult> delivered;
  std::shared_ptr<pnga::analysis_engine::TraceInspectorBundle> bundle;
  orchestrator.setResultCallback([&](const auto& result) {
    std::lock_guard<std::mutex> lock(mutex);
    delivered = std::make_shared<pnga::analysis_engine::TraceQueryResult>(result);
    bundle = std::make_shared<pnga::analysis_engine::TraceInspectorBundle>(
        pnga::analysis_engine::build_trace_inspector_bundle(result));
    cv.notify_all();
  });
  REQUIRE(orchestrator.open(shared_source(encoded.png_bytes), 1u << 20));
  REQUIRE(orchestrator.has_index());
  REQUIRE(orchestrator.document_generation() == 1);
  const auto fast = orchestrator.fast_index();
  REQUIRE(fast.status ==
          pnga::analysis_engine::FastCompressionIndexStatus::kReady);
  REQUIRE(fast.generation == 1);
  REQUIRE_FALSE(fast.blocks.empty());
  REQUIRE(fast.stream.total_output_bytes == encoded.filtered.size());

  TraceOrchestrationRequest request;
  request.generation = orchestrator.document_generation();
  request.selection.stage = pnga::trace_model::Stage::kTrace;
  request.inflated_end = encoded.filtered.size();
  request.max_tokens = 100000;
  request.trace_output_budget_bytes = 1u << 20;
  const auto handle = orchestrator.submit(request);
  REQUIRE(handle.status == TraceSubmitStatus::kQueued);
  REQUIRE(handle.accepted());

  std::unique_lock<std::mutex> lock(mutex);
  REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] {
    return delivered != nullptr;
  }));
  REQUIRE(delivered->status == TraceQueryStatus::kReady);
  REQUIRE(delivered->generation == 1);
  REQUIRE_FALSE(delivered->tokens.empty());
  REQUIRE(bundle->generation == delivered->generation);
  REQUIRE(bundle->block.status ==
          pnga::analysis_engine::BlockInspectorStatus::kReady);
  REQUIRE(bundle->huffman.status ==
          pnga::analysis_engine::HuffmanInspectorStatus::kReady);
  REQUIRE(bundle->decode.status ==
          pnga::analysis_engine::DecodeTraceInspectorStatus::kReady);
}

TEST_CASE("Trace orchestrator rejects stale and over-budget submissions",
          "[analysis-engine][wp5t0b]") {
  const auto encoded =
      pnga_test::encode_png(8, 8, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  TraceOrchestrator orchestrator(/*worker_count=*/0, /*budget=*/1024);
  REQUIRE(orchestrator.open(shared_source(encoded.png_bytes), 1u << 20));

  TraceOrchestrationRequest request;
  request.generation = orchestrator.document_generation();
  request.inflated_end = encoded.filtered.size();
  request.max_tokens = 64;
  request.trace_output_budget_bytes = 512;
  const auto queued = orchestrator.submit(request);
  REQUIRE(queued.status == TraceSubmitStatus::kQueued);
  REQUIRE(orchestrator.queued_tasks() == 1);

  request.generation = 0;
  const auto stale = orchestrator.submit(request);
  REQUIRE(stale.status == TraceSubmitStatus::kStaleGeneration);

  request.generation = orchestrator.document_generation();
  request.trace_output_budget_bytes = 2048;
  const auto over_budget = orchestrator.submit(request);
  REQUIRE(over_budget.status == TraceSubmitStatus::kRejected);

  REQUIRE(orchestrator.cancel(queued));
  orchestrator.setDocumentGeneration(2);
  REQUIRE(orchestrator.queued_tasks() == 0);
}

TEST_CASE("Trace orchestrator does not publish stale queued work",
          "[analysis-engine][wp5t0b]") {
  const auto encoded =
      pnga_test::encode_png(8, 8, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  TraceOrchestrator orchestrator(/*worker_count=*/0, /*budget=*/1024);
  std::size_t callbacks = 0;
  orchestrator.setResultCallback([&](const auto&) { ++callbacks; });
  REQUIRE(orchestrator.open(shared_source(encoded.png_bytes), 1u << 20));

  TraceOrchestrationRequest request;
  request.generation = orchestrator.document_generation();
  request.inflated_end = encoded.filtered.size();
  request.max_tokens = 64;
  request.trace_output_budget_bytes = 512;
  REQUIRE(orchestrator.submit(request).accepted());
  orchestrator.setDocumentGeneration(9);
  REQUIRE(orchestrator.queued_tasks() == 0);
  REQUIRE(callbacks == 0);
}

TEST_CASE("Trace orchestrator refuses to replace an active document",
          "[analysis-engine][wp5t0b]") {
  const auto first =
      pnga_test::encode_png(8, 8, 8, 0, /*interlace=*/false,
                            /*all_none=*/true);
  const auto second = first;
  TraceOrchestrator orchestrator(/*worker_count=*/0, /*budget=*/1024);
  REQUIRE(orchestrator.open(shared_source(first.png_bytes), 1u << 20));

  TraceOrchestrationRequest request;
  request.generation = orchestrator.document_generation();
  request.inflated_end = first.filtered.size();
  request.max_tokens = 64;
  request.trace_output_budget_bytes = 512;
  REQUIRE(orchestrator.submit(request).accepted());
  REQUIRE_FALSE(orchestrator.open(shared_source(second.png_bytes), 1u << 20));
  REQUIRE(orchestrator.last_error() ==
          "cannot replace document while trace replay is active");
  orchestrator.setDocumentGeneration(2);
}
