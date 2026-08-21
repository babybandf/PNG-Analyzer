#ifndef PNGA_ANALYSIS_ENGINE_JOB_SCHEDULER_H
#define PNGA_ANALYSIS_ENGINE_JOB_SCHEDULER_H

// WP-400: cancelable job scheduler with priority and memory reservation
// (REPOSITORY_LAYOUT.md §5.10, ADR-0006). Background indexing, decode and
// deep-trace work runs on a bounded worker pool; the scheduler guarantees that
// a result is delivered only when its job was not cancelled and its document
// generation is still current. Qt-free (ADR-0003).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace pnga::analysis_engine {

// Scheduling priority: the currently selected item beats the visible viewport,
// which beats background statistics (plan WP-400).
enum class JobPriority { kBackground = 0, kViewport = 1, kSelection = 2 };

const char* job_priority_text(JobPriority priority) noexcept;

// Cancellation token shared with a running job. The work function checks
// cancelled() between steps and stops promptly; the scheduler never delivers a
// result for a cancelled job.
class CancellationToken {
 public:
  CancellationToken() = default;
  CancellationToken(const CancellationToken&) = delete;
  CancellationToken& operator=(const CancellationToken&) = delete;

  bool cancelled() const noexcept {
    return cancelled_.load(std::memory_order_acquire);
  }
  void request_cancel() noexcept {
    cancelled_.store(true, std::memory_order_release);
  }

 private:
  std::atomic<bool> cancelled_{false};
};

// Result of one finished job. Delivered to the sink only when the job was not
// cancelled and its generation is current.
struct JobResult {
  std::uint64_t job_id = 0;
  std::uint64_t generation = 0;
  bool success = false;
  std::string error;  // stable message on failure
  std::vector<std::byte> payload;  // optional opaque payload
};

// The work a job performs. Must check `token.cancelled()` between steps and
// fill `result`. Must not call back into the scheduler.
using JobWork = std::function<void(const CancellationToken& token,
                                   JobResult& result)>;

// Bounded worker pool with priority ordering, cooperative cancellation,
// generation-stale suppression and a memory reservation budget.
//
// Contract:
//  - The result sink must be non-blocking and must not re-enter the scheduler
//    (delivery happens while the internal mutex is held).
//  - `worker_count == 0` accepts jobs but never runs them.
class JobScheduler {
 public:
  // `max_reserved_bytes` caps the memory held by concurrently running jobs. A
  // job whose reservation alone exceeds the budget can never run and is
  // rejected by submit().
  JobScheduler(std::size_t worker_count, std::uint64_t max_reserved_bytes);
  ~JobScheduler();  // shutdown()

  JobScheduler(const JobScheduler&) = delete;
  JobScheduler& operator=(const JobScheduler&) = delete;

  // Replaces the result sink (invoked on a worker thread). Expected to be set
  // once before submitting; replacing mid-run is not synchronized.
  void setResultSink(std::function<void(const JobResult&)> sink);

  // Submits a job. Returns a cancellation token the caller can cancel with, or
  // nullptr when shutdown or when `reservation_bytes` exceeds the budget.
  std::shared_ptr<CancellationToken> submit(
      std::uint64_t job_id, std::uint64_t generation, JobPriority priority,
      std::uint64_t reservation_bytes, JobWork work);

  // Starts a new document generation: queued jobs with an older generation are
  // dropped, and results of running stale jobs are discarded at completion.
  void setDocumentGeneration(std::uint64_t generation);

  // Cancels nothing explicitly, but stops workers: queued jobs are dropped and
  // running jobs finish (their results are not delivered). Idempotent; workers
  // are joined before this returns.
  void shutdown();

  std::size_t worker_count() const noexcept { return workers_.size(); }
  std::size_t queued_count() const noexcept;
  std::uint64_t running_reserved_bytes() const noexcept;
  std::uint64_t max_reserved_bytes() const noexcept { return max_reserved_; }

 private:
  struct QueuedJob {
    std::uint64_t id;
    std::uint64_t generation;
    JobPriority priority;
    std::uint64_t reservation;
    std::uint64_t seq;
    std::shared_ptr<CancellationToken> token;
    JobWork work;
  };

  struct JobOrder {
    bool operator()(const QueuedJob& a, const QueuedJob& b) const noexcept {
      if (a.priority != b.priority) {
        return a.priority > b.priority;  // higher priority first
      }
      return a.seq < b.seq;  // FIFO within a priority
    }
  };

  void workerLoop();
  bool hasRunnableLocked() const;
  bool runnable(const QueuedJob& job) const noexcept;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::thread> workers_;
  std::set<QueuedJob, JobOrder> queue_;
  std::function<void(const JobResult&)> sink_;
  std::uint64_t generation_ = 0;
  std::uint64_t max_reserved_ = 0;
  std::uint64_t running_reserved_ = 0;
  std::uint64_t next_seq_ = 0;
  bool shutdown_ = false;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_JOB_SCHEDULER_H
