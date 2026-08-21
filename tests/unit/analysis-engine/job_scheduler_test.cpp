// WP-400 job scheduler tests: priority ordering, cooperative cancellation
// (no result after cancel), generation staleness suppression, memory
// reservation budget, clean shutdown and a submit/cancel/generation stress.

#include <pnga/analysis-engine/job_scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using pnga::analysis_engine::CancellationToken;
using pnga::analysis_engine::JobPriority;
using pnga::analysis_engine::JobResult;
using pnga::analysis_engine::JobScheduler;
using pnga::analysis_engine::JobWork;

namespace {

JobWork quick_ok() {
  return [](const CancellationToken&, JobResult& r) { r.success = true; };
}

}  // namespace

TEST_CASE("Higher priority jobs run first", "[analysis-engine][wp400]") {
  JobScheduler scheduler(1, 1024);
  scheduler.setDocumentGeneration(1);  // a document is open (real GUI flow)
  std::vector<std::uint64_t> order;
  std::mutex order_mutex;
  std::atomic<int> delivered_count{0};
  scheduler.setResultSink([&](const JobResult& r) {
    {
      std::lock_guard<std::mutex> lock(order_mutex);
      order.push_back(r.job_id);
    }
    ++delivered_count;
  });

  // Submit background first, then viewport, then selection: the scheduler must
  // still run them selection, viewport, background (plan priority).
  scheduler.submit(1, 1, JobPriority::kBackground, 0, quick_ok());
  scheduler.submit(2, 1, JobPriority::kViewport, 0, quick_ok());
  scheduler.submit(3, 1, JobPriority::kSelection, 0, quick_ok());

  // Wait for the worker to finish all three before shutting down (drop-on-
  // shutdown would otherwise discard not-yet-run jobs).
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (delivered_count.load() < 3 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  scheduler.shutdown();
  std::lock_guard<std::mutex> lock(order_mutex);
  REQUIRE(order == std::vector<std::uint64_t>({3, 2, 1}));
}

TEST_CASE("Cancelled running job delivers no result", "[analysis-engine][wp400]") {
  JobScheduler scheduler(1, 1024);
  scheduler.setDocumentGeneration(1);
  std::atomic<int> delivered{0};
  std::atomic<bool> work_done{false};
  std::promise<void> started;
  scheduler.setResultSink([&](const JobResult&) { ++delivered; });

  const auto token = scheduler.submit(
      7, 1, JobPriority::kSelection, 100,
      [&](const CancellationToken& t, JobResult& r) {
        started.set_value();
        while (!t.cancelled()) {
          std::this_thread::yield();
        }
        work_done = true;
        r.success = true;
      });
  REQUIRE(token != nullptr);
  started.get_future().wait();  // the job is running
  token->request_cancel();
  while (!work_done.load()) {
    std::this_thread::yield();  // the job observed the cancellation
  }
  while (scheduler.running_reserved_bytes() != 0) {
    std::this_thread::yield();  // the worker finished the drop decision
  }
  REQUIRE(delivered.load() == 0);
  scheduler.shutdown();
  REQUIRE(delivered.load() == 0);
}

TEST_CASE("Stale generation results are dropped", "[analysis-engine][wp400]") {
  JobScheduler scheduler(1, 1024);
  scheduler.setDocumentGeneration(1);
  std::atomic<int> delivered{0};
  std::atomic<bool> block_done{false};
  std::promise<void> started;
  scheduler.setResultSink([&](const JobResult&) { ++delivered; });

  // A blocking job keeps the single worker busy.
  const auto block_token = scheduler.submit(
      1, 1, JobPriority::kViewport, 100,
      [&](const CancellationToken& t, JobResult& r) {
        started.set_value();
        while (!t.cancelled()) {
          std::this_thread::yield();
        }
        block_done = true;
        r.success = true;
      });
  started.get_future().wait();

  // A queued job of the old generation is dropped when the generation moves on.
  scheduler.submit(2, 1, JobPriority::kBackground, 0, quick_ok());
  REQUIRE(scheduler.queued_count() == 1);
  scheduler.setDocumentGeneration(2);
  REQUIRE(scheduler.queued_count() == 0);

  // Let the running generation-1 job finish; its result is stale and dropped.
  block_token->request_cancel();
  while (!block_done.load()) {
    std::this_thread::yield();
  }
  while (scheduler.running_reserved_bytes() != 0) {
    std::this_thread::yield();
  }
  REQUIRE(delivered.load() == 0);
  scheduler.shutdown();
  REQUIRE(delivered.load() == 0);
}

TEST_CASE("Memory reservation holds over-budget jobs and rejects impossible ones",
          "[analysis-engine][wp400]") {
  JobScheduler scheduler(2, 100);
  scheduler.setDocumentGeneration(1);

  // A job whose reservation alone exceeds the budget can never run.
  const auto impossible = scheduler.submit(9, 1, JobPriority::kSelection, 101,
                                           quick_ok());
  REQUIRE(impossible == nullptr);

  // Two 60-byte jobs cannot run concurrently under a 100-byte budget, even
  // though two workers are available.
  std::mutex log_mutex;
  std::vector<std::string> log;
  scheduler.setResultSink([&](const JobResult&) {});
  scheduler.submit(1, 1, JobPriority::kBackground, 60,
                   [&](const CancellationToken&, JobResult& r) {
                     {
                       std::lock_guard<std::mutex> lock(log_mutex);
                       log.push_back("a-start");
                     }
                     std::this_thread::sleep_for(std::chrono::milliseconds(20));
                     {
                       std::lock_guard<std::mutex> lock(log_mutex);
                       log.push_back("a-end");
                     }
                     r.success = true;
                   });
  scheduler.submit(2, 1, JobPriority::kBackground, 60,
                   [&](const CancellationToken&, JobResult& r) {
                     std::lock_guard<std::mutex> lock(log_mutex);
                     log.push_back("b-start");
                     r.success = true;
                   });

  while (scheduler.queued_count() != 0 || scheduler.running_reserved_bytes() != 0) {
    std::this_thread::yield();
  }
  scheduler.shutdown();

  // "a-end" must precede "b-start": B only ran once A freed its reservation.
  std::size_t a_end = log.size();
  std::size_t b_start = log.size();
  for (std::size_t i = 0; i < log.size(); ++i) {
    if (log[i] == "a-end") {
      a_end = i;
    } else if (log[i] == "b-start") {
      b_start = i;
    }
  }
  REQUIRE(a_end < log.size());
  REQUIRE(b_start < log.size());
  REQUIRE(a_end < b_start);
}

TEST_CASE("Shutdown joins workers and rejects new submissions",
          "[analysis-engine][wp400]") {
  JobScheduler scheduler(2, 1024);
  scheduler.setDocumentGeneration(1);
  std::atomic<int> delivered{0};
  scheduler.setResultSink([&](const JobResult&) { ++delivered; });
  scheduler.submit(1, 1, JobPriority::kBackground, 0, quick_ok());
  scheduler.submit(2, 1, JobPriority::kViewport, 0, quick_ok());

  scheduler.shutdown();  // must join without deadlock
  REQUIRE(scheduler.queued_count() == 0);
  REQUIRE(scheduler.running_reserved_bytes() == 0);

  const auto after = scheduler.submit(3, 1, JobPriority::kBackground, 0,
                                      quick_ok());
  REQUIRE(after == nullptr);  // shutdown rejects new work
}

TEST_CASE("Rapid submit/cancel/generation changes stay consistent",
          "[analysis-engine][wp400]") {
  JobScheduler scheduler(4, 4096);
  scheduler.setDocumentGeneration(1);
  std::atomic<int> delivered{0};
  scheduler.setResultSink([&](const JobResult&) { ++delivered; });

  std::uint64_t generation = 1;
  for (int round = 0; round < 300; ++round) {
    if (round % 7 == 0) {
      scheduler.setDocumentGeneration(++generation);
    }
    const auto token = scheduler.submit(
        static_cast<std::uint64_t>(round), generation,
        JobPriority::kBackground, 0, quick_ok());
    if (token != nullptr && round % 3 == 0) {
      token->request_cancel();
    }
  }
  scheduler.shutdown();
  REQUIRE(scheduler.queued_count() == 0);
  REQUIRE(scheduler.running_reserved_bytes() == 0);
  REQUIRE(delivered.load() >= 0);  // smoke: no crash, no deadlock
}
