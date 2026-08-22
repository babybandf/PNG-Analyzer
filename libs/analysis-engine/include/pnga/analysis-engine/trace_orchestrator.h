#ifndef PNGA_ANALYSIS_ENGINE_TRACE_ORCHESTRATOR_H
#define PNGA_ANALYSIS_ENGINE_TRACE_ORCHESTRATOR_H

// WP-5T0B: bounded, cancelable orchestration around the WP-5T0A contract.
// The worker replays only to the requested output boundary and publishes only
// when its document generation is still current. Decoder and Virtual IDAT
// ownership remain in their existing modules.

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/trace_query.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pnga::analysis_engine {

enum class TraceSubmitStatus {
  kQueued = 0,
  kNotIndexed = 1,
  kStaleGeneration = 2,
  kRejected = 3,
};

const char* trace_submit_status_text(TraceSubmitStatus status) noexcept;

struct TraceOrchestrationRequest {
  std::uint64_t generation = 0;
  pnga::trace_model::Selection selection;
  std::uint64_t inflated_begin = 0;
  std::uint64_t inflated_end = 0;  // exclusive
  std::uint64_t max_tokens = 0;
  std::uint64_t trace_output_budget_bytes = 0;
  JobPriority priority = JobPriority::kSelection;
};

struct TraceTaskHandle {
  TraceSubmitStatus status = TraceSubmitStatus::kRejected;
  std::string error;
  std::uint64_t job_id = 0;
  std::uint64_t generation = 0;
  std::shared_ptr<CancellationToken> cancellation;

  bool accepted() const noexcept {
    return status == TraceSubmitStatus::kQueued && cancellation != nullptr;
  }
};

// One document's trace coordinator. Set the callback before submit(); the
// callback runs on a worker thread and must not re-enter this coordinator.
// Results are immutable values and are never delivered for stale/cancelled
// jobs. `open()` is intentionally rejected while a previous replay is active.
class TraceOrchestrator final {
 public:
  TraceOrchestrator(std::size_t worker_count,
                    std::uint64_t max_reserved_bytes);
  ~TraceOrchestrator();

  TraceOrchestrator(const TraceOrchestrator&) = delete;
  TraceOrchestrator& operator=(const TraceOrchestrator&) = delete;

  bool open(std::shared_ptr<const pnga::io::IByteSource> source,
            std::uint64_t max_index_output_bytes);

  void setResultCallback(std::function<void(const TraceQueryResult&)> cb);

  TraceTaskHandle submit(const TraceOrchestrationRequest& request);

  // Cooperative cancellation. The callback is not invoked for a cancelled
  // task, matching JobScheduler's stale-result contract.
  bool cancel(const TraceTaskHandle& handle) noexcept;

  // In-flight work from older generations is discarded before publication.
  void setDocumentGeneration(std::uint64_t generation);

  bool has_index() const noexcept;
  std::uint64_t document_generation() const noexcept;
  std::size_t queued_tasks() const noexcept;
  std::string last_error() const;

 private:
  void onJobResult(const JobResult& result);

  JobScheduler scheduler_;
  mutable std::mutex mutex_;
  std::function<void(const TraceQueryResult&)> callback_;
  std::unordered_map<std::uint64_t, TraceQueryResult> completed_;
  std::shared_ptr<const pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  std::unique_ptr<pnga::png_format::VirtualIDATStream> stream_;
  pnga::deflate_index::BlockIndexResult block_index_;
  std::uint64_t generation_ = 0;
  std::uint64_t next_job_id_ = 1;
  std::string last_error_;
  bool has_index_ = false;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_TRACE_ORCHESTRATOR_H
