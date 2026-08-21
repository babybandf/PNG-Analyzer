// WP-406 query coordinator tests: open -> indexed, query -> replaying -> ready
// with unfiltered bytes matching the full decode, cached ready results,
// priority, errors and generation reset.

#include <pnga/analysis-engine/query_coordinator.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include "test_png_helpers.h"

using namespace pnga_test;  // NOLINT: test helpers are the local vocabulary

using pnga::analysis_engine::JobPriority;
using pnga::analysis_engine::QueryCoordinator;
using pnga::analysis_engine::QueryStatus;
using pnga::io::MemoryByteSource;

namespace {

std::shared_ptr<const pnga::io::IByteSource> shared_source(
    const std::vector<std::byte>& bytes) {
  return std::make_shared<MemoryByteSource>(bytes);
}

// Waits until `ready_row` reaches `status` (via the callback), with a timeout.
bool wait_status(QueryCoordinator& coordinator, std::uint64_t row,
                 QueryStatus status, int timeout_ms = 5000) {
  std::mutex m;
  std::condition_variable cv;
  std::atomic<bool> done{false};
  coordinator.setStatusCallback(
      [&](std::uint64_t r, QueryStatus s) {
        if (r == row && s == status) {
          done = true;
          cv.notify_all();
        }
      });
  std::unique_lock<std::mutex> lock(m);
  return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [&] { return done.load(); });
}

}  // namespace

TEST_CASE("Open indexes the document; rows are indexed before any query",
          "[analysis-engine][wp406]") {
  const EncodedPng e = encode_png(24, 37, 8, 6, false, false);
  QueryCoordinator coordinator(/*worker_count=*/2, /*budget=*/1u << 20);
  REQUIRE(coordinator.open(shared_source(e.png_bytes), e.header, 512));
  REQUIRE(coordinator.has_index());
  REQUIRE(coordinator.scanline_count() == 37);
  const auto st = coordinator.status_for(10);
  REQUIRE(st.status == QueryStatus::kIndexed);
}

TEST_CASE("Query replays a scanline to ready with the decoded bytes",
          "[analysis-engine][wp406]") {
  const EncodedPng e = encode_png(24, 37, 8, 6, false, false);
  QueryCoordinator coordinator(/*worker_count=*/2, /*budget=*/1u << 20);
  REQUIRE(coordinator.open(shared_source(e.png_bytes), e.header, 512));

  const std::uint64_t row = 21;
  const auto first = coordinator.query_scanline(row, JobPriority::kSelection);
  REQUIRE(first.status == QueryStatus::kReplaying);

  REQUIRE(wait_status(coordinator, row, QueryStatus::kReady));
  const auto result = coordinator.query_scanline(row, JobPriority::kSelection);
  REQUIRE(result.status == QueryStatus::kReady);

  const std::uint64_t rb = test_row_bytes(24, 8, 6);  // 96
  REQUIRE(result.unfiltered ==
          std::vector<std::byte>(e.raw.begin() + static_cast<std::ptrdiff_t>(row * rb),
                                 e.raw.begin() +
                                     static_cast<std::ptrdiff_t>((row + 1) * rb)));
}

TEST_CASE("Ready rows are served from cache without resubmitting",
          "[analysis-engine][wp406]") {
  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  QueryCoordinator coordinator(/*worker_count=*/1, /*budget=*/1u << 20);
  REQUIRE(coordinator.open(shared_source(e.png_bytes), e.header, 64));

  REQUIRE(coordinator.query_scanline(3, JobPriority::kViewport).status ==
          QueryStatus::kReplaying);
  REQUIRE(wait_status(coordinator, 3, QueryStatus::kReady));
  REQUIRE(coordinator.query_scanline(3, JobPriority::kViewport).status ==
          QueryStatus::kReady);
  // No queued replay remains for the cached row.
  REQUIRE(coordinator.queued_replays() == 0);
}

TEST_CASE("Out-of-range and unopened queries report error",
          "[analysis-engine][wp406]") {
  QueryCoordinator unopened(1, 1u << 20);
  const auto before = unopened.query_scanline(0, JobPriority::kBackground);
  REQUIRE(before.status == QueryStatus::kError);

  const EncodedPng e = encode_png(8, 8, 8, 6, false, true);
  QueryCoordinator coordinator(1, 1u << 20);
  REQUIRE(coordinator.open(shared_source(e.png_bytes), e.header, 64));
  const auto bad = coordinator.query_scanline(1000, JobPriority::kBackground);
  REQUIRE(bad.status == QueryStatus::kError);
  REQUIRE_FALSE(bad.error.empty());
}

TEST_CASE("Generation reset returns rows to indexed and rejects no stale data",
          "[analysis-engine][wp406]") {
  const EncodedPng e = encode_png(16, 16, 8, 6, false, true);
  QueryCoordinator coordinator(1, 1u << 20);
  REQUIRE(coordinator.open(shared_source(e.png_bytes), e.header, 128));

  REQUIRE(coordinator.query_scanline(5, JobPriority::kSelection).status ==
          QueryStatus::kReplaying);
  REQUIRE(wait_status(coordinator, 5, QueryStatus::kReady));
  REQUIRE(coordinator.status_for(5).status == QueryStatus::kReady);

  coordinator.setDocumentGeneration(99);  // a fresh document generation
  REQUIRE(coordinator.status_for(5).status == QueryStatus::kIndexed);

  // A fresh query replays again and is correct.
  REQUIRE(coordinator.query_scanline(5, JobPriority::kSelection).status ==
          QueryStatus::kReplaying);
  REQUIRE(wait_status(coordinator, 5, QueryStatus::kReady));
  const std::uint64_t rb = test_row_bytes(16, 8, 6);  // 64
  const auto res = coordinator.query_scanline(5, JobPriority::kSelection);
  REQUIRE(res.unfiltered ==
          std::vector<std::byte>(e.raw.begin() + static_cast<std::ptrdiff_t>(5 * rb),
                                 e.raw.begin() +
                                     static_cast<std::ptrdiff_t>(6 * rb)));
}
