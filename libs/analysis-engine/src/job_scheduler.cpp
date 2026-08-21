// WP-400 job scheduler implementation. A set ordered by (priority, seq) is the
// ready queue; workers pick the highest-priority job whose reservation fits the
// budget. Results are delivered under the internal mutex after a
// cancelled/stale check, so the sink must be fast and non-reentrant.

#include "pnga/analysis-engine/job_scheduler.h"

#include <utility>

namespace pnga::analysis_engine {

const char* job_priority_text(JobPriority priority) noexcept {
  switch (priority) {
    case JobPriority::kBackground:
      return "background";
    case JobPriority::kViewport:
      return "viewport";
    case JobPriority::kSelection:
      return "selection";
  }
  return "unknown";
}

JobScheduler::JobScheduler(std::size_t worker_count,
                           std::uint64_t max_reserved_bytes)
    : max_reserved_(max_reserved_bytes) {
  for (std::size_t i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this] { workerLoop(); });
  }
}

JobScheduler::~JobScheduler() { shutdown(); }

void JobScheduler::setResultSink(std::function<void(const JobResult&)> sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  sink_ = std::move(sink);
}

std::shared_ptr<CancellationToken> JobScheduler::submit(
    std::uint64_t job_id, std::uint64_t generation, JobPriority priority,
    std::uint64_t reservation_bytes, JobWork work) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (shutdown_ || reservation_bytes > max_reserved_) {
    return nullptr;  // would never run
  }
  auto token = std::make_shared<CancellationToken>();
  queue_.insert(QueuedJob{job_id, generation, priority, reservation_bytes,
                          next_seq_++, token, std::move(work)});
  cv_.notify_one();
  return token;
}

void JobScheduler::setDocumentGeneration(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  generation_ = generation;
  for (auto it = queue_.begin(); it != queue_.end();) {
    if (it->generation < generation) {
      it = queue_.erase(it);  // stale queued job never runs
    } else {
      ++it;
    }
  }
  cv_.notify_all();
}

void JobScheduler::shutdown() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
  queue_.clear();  // queued jobs are dropped
  cv_.notify_all();
  lock.unlock();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

std::size_t JobScheduler::queued_count() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

std::uint64_t JobScheduler::running_reserved_bytes() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_reserved_;
}

bool JobScheduler::runnable(const QueuedJob& job) const noexcept {
  return job.reservation + running_reserved_ <= max_reserved_;
}

bool JobScheduler::hasRunnableLocked() const {
  for (const auto& job : queue_) {
    if (runnable(job)) {
      return true;
    }
  }
  return false;
}

void JobScheduler::workerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    cv_.wait(lock, [this] {
      return shutdown_ || (hasRunnableLocked() && !queue_.empty());
    });
    if (shutdown_ && queue_.empty()) {
      break;
    }
    if (!hasRunnableLocked()) {
      continue;  // only non-runnable jobs remain; wait for memory to free
    }

    // Highest-priority runnable job (a higher-priority non-runnable job waits).
    auto it = queue_.begin();
    while (it != queue_.end() && !runnable(*it)) {
      ++it;
    }
    QueuedJob job = std::move(*it);
    queue_.erase(it);
    running_reserved_ += job.reservation;
    lock.unlock();

    JobResult result;
    result.job_id = job.id;
    result.generation = job.generation;
    if (job.token->cancelled()) {
      result.error = "job cancelled before start";
    } else {
      try {
        job.work(*job.token, result);
      } catch (...) {
        result.success = false;
        result.error = "job threw an exception";
      }
    }

    lock.lock();
    running_reserved_ -= job.reservation;
    const bool deliver =
        !shutdown_ && !job.token->cancelled() &&
        job.generation == generation_ && sink_ != nullptr;
    if (deliver) {
      try {
        sink_(result);
      } catch (...) {
        // A misbehaving sink must not kill the worker pool.
      }
    }
    cv_.notify_all();  // freed reservation may make a waiting job runnable
  }
}

}  // namespace pnga::analysis_engine
