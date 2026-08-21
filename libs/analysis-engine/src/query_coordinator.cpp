// WP-406 query coordinator implementation. The anchor index is built once; a
// missing row's unfiltered state is materialized by a priority-ordered replay
// job whose result is delivered through the scheduler's sink, which updates the
// per-row state and fires the status callback.

#include "pnga/analysis-engine/query_coordinator.h"

#include <utility>

namespace pnga::analysis_engine {

const char* query_status_text(QueryStatus status) noexcept {
  switch (status) {
    case QueryStatus::kIndexed:
      return "indexed";
    case QueryStatus::kReplaying:
      return "replaying";
    case QueryStatus::kReady:
      return "ready";
    case QueryStatus::kError:
      return "error";
  }
  return "unknown";
}

QueryCoordinator::QueryCoordinator(std::size_t worker_count,
                                   std::uint64_t replay_budget_bytes)
    : scheduler_(worker_count, replay_budget_bytes) {
  // Result sink runs on a worker thread; it must not re-enter the coordinator.
  scheduler_.setResultSink([this](const JobResult& r) {
    const QueryStatus status =
        r.success ? QueryStatus::kReady : QueryStatus::kError;
    std::function<void(std::uint64_t, QueryStatus)> cb;
    {
      std::lock_guard<std::mutex> lock(rows_mutex_);
      RowState& state = rows_[r.job_id];
      state.status = status;
      state.unfiltered = r.payload;
      state.error = r.error;
      cb = on_status_;
    }
    if (cb) {
      cb(r.job_id, status);
    }
  });
}

QueryCoordinator::~QueryCoordinator() {
  scheduler_.shutdown();  // join workers before members die
}

bool QueryCoordinator::open(std::shared_ptr<const pnga::io::IByteSource> source,
                            const pnga::png_reconstruction::ImageHeader& header,
                            std::uint64_t anchor_interval_bytes) {
  // Refuse to re-open while replay jobs are in flight (the coordinator's
  // members would be replaced under them).
  {
    std::lock_guard<std::mutex> lock(rows_mutex_);
    for (const auto& [row, state] : rows_) {
      if (state.status == QueryStatus::kReplaying) {
        return false;
      }
    }
  }
  if (scheduler_.queued_count() != 0) {
    return false;
  }

  source_ = std::move(source);
  header_ = header;
  index_ = pnga::png_format::index_chunks(*source_);
  stream_ = std::make_unique<pnga::png_format::VirtualIDATStream>(index_);
  anchors_ = build_scanline_anchors(*stream_, *source_, header_,
                                    anchor_interval_bytes, 1ull << 30);
  if (!anchors_.success) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(rows_mutex_);
    rows_.clear();
  }
  has_index_ = true;
  scanline_count_ = anchors_.scanline_count;
  scheduler_.setDocumentGeneration(++generation_);
  return true;
}

std::uint64_t QueryCoordinator::row_reservation(std::uint64_t row) const noexcept {
  // Row data plus a scratch buffer for the extraction replay.
  const std::uint64_t row_bytes =
      row < anchors_.scanlines.size() ? anchors_.scanlines[row].length : 0;
  return row_bytes + (1ull << 16);
}

ScanlineQueryResult QueryCoordinator::query_scanline(std::uint64_t row,
                                                     JobPriority priority) {
  if (!has_index_) {
    return {QueryStatus::kError, row, {}, "coordinator not open"};
  }
  if (row >= scanline_count_) {
    return {QueryStatus::kError, row, {}, "scanline out of range"};
  }
  {
    std::lock_guard<std::mutex> lock(rows_mutex_);
    RowState& state = rows_[row];
    if (state.status == QueryStatus::kReady) {
      return {QueryStatus::kReady, row, state.unfiltered, {}};
    }
    if (state.status == QueryStatus::kReplaying) {
      return {QueryStatus::kReplaying, row, {}, {}};
    }
    state.status = QueryStatus::kReplaying;  // kIndexed/kError -> (re)submit
  }

  const std::uint64_t generation = generation_;
  const auto token = scheduler_.submit(
      row, generation, priority, row_reservation(row),
      [this, row, generation](const CancellationToken& token, JobResult& r) {
        r.job_id = row;
        r.generation = generation;
        if (token.cancelled()) {
          return;
        }
        const RowRestoreResult res =
            restore_scanline(anchors_, *stream_, *source_, row);
        r.success = res.success;
        r.error = res.error;
        r.payload = res.unfiltered;
      });
  if (token == nullptr) {
    std::lock_guard<std::mutex> lock(rows_mutex_);
    RowState& state = rows_[row];
    state.status = QueryStatus::kError;
    state.error = "replay job rejected (budget or shutdown)";
    return {QueryStatus::kError, row, {}, state.error};
  }
  return {QueryStatus::kReplaying, row, {}, {}};
}

ScanlineQueryResult QueryCoordinator::status_for(std::uint64_t row) const {
  if (!has_index_) {
    return {QueryStatus::kError, row, {}, "coordinator not open"};
  }
  if (row >= scanline_count_) {
    return {QueryStatus::kError, row, {}, "scanline out of range"};
  }
  std::lock_guard<std::mutex> lock(rows_mutex_);
  const auto it = rows_.find(row);
  if (it == rows_.end()) {
    return {QueryStatus::kIndexed, row, {}, {}};
  }
  return {it->second.status, row, it->second.unfiltered, it->second.error};
}

void QueryCoordinator::setDocumentGeneration(std::uint64_t generation) {
  generation_ = generation;
  scheduler_.setDocumentGeneration(generation);
  std::lock_guard<std::mutex> lock(rows_mutex_);
  rows_.clear();  // all rows reset to "indexed"
}

void QueryCoordinator::setStatusCallback(
    std::function<void(std::uint64_t, QueryStatus)> cb) {
  std::lock_guard<std::mutex> lock(rows_mutex_);
  on_status_ = std::move(cb);
}

}  // namespace pnga::analysis_engine
