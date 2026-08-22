// WP-5T0B orchestration implementation. Work is bounded by the request and
// scheduler reservation; stale generations are checked before storing results.

#include "pnga/analysis-engine/trace_orchestrator.h"

#include "virtual_idat_source.h"

#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <limits>
#include <utility>

namespace pnga::analysis_engine {

const char* trace_submit_status_text(TraceSubmitStatus status) noexcept {
  switch (status) {
    case TraceSubmitStatus::kQueued:
      return "queued";
    case TraceSubmitStatus::kNotIndexed:
      return "not indexed";
    case TraceSubmitStatus::kStaleGeneration:
      return "stale generation";
    case TraceSubmitStatus::kRejected:
      return "rejected";
  }
  return "unknown";
}

TraceOrchestrator::TraceOrchestrator(std::size_t worker_count,
                                     std::uint64_t max_reserved_bytes)
    : scheduler_(worker_count, max_reserved_bytes) {
  scheduler_.setResultSink([this](const JobResult& result) {
    onJobResult(result);
  });
}

TraceOrchestrator::~TraceOrchestrator() { scheduler_.shutdown(); }

bool TraceOrchestrator::open(
    std::shared_ptr<const pnga::io::IByteSource> source,
    std::uint64_t max_index_output_bytes) {
  if (source == nullptr || max_index_output_bytes == 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = "trace source or index budget is invalid";
    return false;
  }
  if (scheduler_.queued_count() != 0 || scheduler_.running_reserved_bytes() != 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = "cannot replace document while trace replay is active";
    return false;
  }

  auto index = pnga::png_format::index_chunks(*source);
  if (!index.valid_signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = "invalid PNG signature";
    return false;
  }
  auto stream = std::make_unique<pnga::png_format::VirtualIDATStream>(index);
  VirtualIdatSource logical(*stream, *source);
  auto block_index = pnga::deflate_index::index_blocks(
      logical, max_index_output_bytes);
  if (!block_index.success) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = block_index.error.empty() ? "trace block index failed"
                                            : block_index.error;
    return false;
  }

  std::uint64_t next_generation = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
      last_error_ = "document generation overflow";
      return false;
    }
    next_generation = generation_ + 1;
    source_ = std::move(source);
    index_ = std::move(index);
    stream_ = std::move(stream);
    block_index_ = std::move(block_index);
    completed_.clear();
    has_index_ = true;
    last_error_.clear();
    generation_ = next_generation;
  }
  scheduler_.setDocumentGeneration(next_generation);
  return true;
}

void TraceOrchestrator::setResultCallback(
    std::function<void(const TraceQueryResult&)> cb) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(cb);
}

TraceTaskHandle TraceOrchestrator::submit(
    const TraceOrchestrationRequest& request) {
  TraceTaskHandle out;
  std::shared_ptr<const pnga::io::IByteSource> source;
  pnga::png_format::VirtualIDATStream* stream = nullptr;
  pnga::deflate_index::BlockIndexResult block_index;
  std::uint64_t job_id = 0;
  std::uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_index_ || source_ == nullptr || stream_ == nullptr) {
      out.status = TraceSubmitStatus::kNotIndexed;
      out.error = "trace document is not indexed";
      last_error_ = out.error;
      return out;
    }
    if (request.generation != generation_) {
      out.status = TraceSubmitStatus::kStaleGeneration;
      out.error = "trace request generation is stale";
      last_error_ = out.error;
      return out;
    }
    if (request.max_tokens == 0 || request.trace_output_budget_bytes == 0) {
      out.status = TraceSubmitStatus::kRejected;
      out.error = "trace request budget is zero";
      last_error_ = out.error;
      return out;
    }
    if (request.inflated_end < request.inflated_begin ||
        request.inflated_end > block_index_.total_output_bytes) {
      out.status = TraceSubmitStatus::kRejected;
      out.error = "trace request range is out of bounds";
      last_error_ = out.error;
      return out;
    }
    if (next_job_id_ == std::numeric_limits<std::uint64_t>::max()) {
      out.status = TraceSubmitStatus::kRejected;
      out.error = "trace job id overflow";
      last_error_ = out.error;
      return out;
    }
    job_id = next_job_id_++;
    generation = generation_;
    source = source_;
    stream = stream_.get();
    block_index = block_index_;
  }

  const std::uint64_t reservation = request.trace_output_budget_bytes;
  const auto token = scheduler_.submit(
      job_id, generation, request.priority, reservation,
      [this, request, source = std::move(source), stream, block_index,
       job_id, generation](const CancellationToken& cancellation,
                            JobResult& result) {
        if (cancellation.cancelled()) {
          return;
        }
        VirtualIdatSource logical(*stream, *source);
        const auto trace = pnga::deflate_trace::decode_stored_and_fixed(
            logical, request.trace_output_budget_bytes);
        if (cancellation.cancelled()) {
          return;
        }
        TraceQueryResult query = compose_trace_query(
            generation, request.selection, block_index, trace, *stream, *source,
            request.inflated_begin, request.inflated_end, request.max_tokens);
        if (cancellation.cancelled()) {
          return;
        }
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (cancellation.cancelled() || generation != generation_ ||
              !has_index_) {
            return;
          }
          completed_[job_id] = std::move(query);
        }
        result.success = true;
      });
  if (token == nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.status = TraceSubmitStatus::kRejected;
    out.error = "trace replay job rejected (budget or shutdown)";
    last_error_ = out.error;
    return out;
  }
  out.status = TraceSubmitStatus::kQueued;
  out.job_id = job_id;
  out.generation = generation;
  out.cancellation = token;
  return out;
}

bool TraceOrchestrator::cancel(const TraceTaskHandle& handle) noexcept {
  if (!handle.accepted() || handle.generation != document_generation()) {
    return false;
  }
  handle.cancellation->request_cancel();
  std::lock_guard<std::mutex> lock(mutex_);
  completed_.erase(handle.job_id);
  return true;
}

void TraceOrchestrator::setDocumentGeneration(std::uint64_t generation) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = generation;
    completed_.clear();
  }
  scheduler_.setDocumentGeneration(generation);
}

bool TraceOrchestrator::has_index() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_index_;
}

std::uint64_t TraceOrchestrator::document_generation() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

std::size_t TraceOrchestrator::queued_tasks() const noexcept {
  return scheduler_.queued_count();
}

std::string TraceOrchestrator::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

void TraceOrchestrator::onJobResult(const JobResult& result) {
  TraceQueryResult query;
  std::function<void(const TraceQueryResult&)> callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = completed_.find(result.job_id);
    if (it == completed_.end() || result.generation != generation_) {
      return;
    }
    query = std::move(it->second);
    completed_.erase(it);
    callback = callback_;
  }
  if (callback) {
    callback(query);
  }
}

}  // namespace pnga::analysis_engine
