#ifndef PNGA_ANALYSIS_ENGINE_QUERY_COORDINATOR_H
#define PNGA_ANALYSIS_ENGINE_QUERY_COORDINATOR_H

// WP-406: large-file end-to-end query coordinator (REPOSITORY_LAYOUT.md §5.10,
// ADR-0006). Builds the scanline anchor index once (status "indexed"), then
// answers per-scanline queries: index information is shown immediately, and a
// missing row's unfiltered state is materialized by a cancelable,
// priority-ordered replay job through the JobScheduler (WP-400). Status
// transitions indexed -> replaying -> ready/error are reported via a callback.
// Qt-free.

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/analysis-engine/scanline_anchor.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pnga::analysis_engine {

enum class QueryStatus { kIndexed = 0, kReplaying = 1, kReady = 2, kError = 3 };

const char* query_status_text(QueryStatus status) noexcept;

struct ScanlineQueryResult {
  QueryStatus status = QueryStatus::kIndexed;
  std::uint64_t row = 0;
  std::vector<std::byte> unfiltered;  // valid when status == kReady
  std::string error;                  // set when status == kError
};

// Coordinates large-file scanline queries over one document. The coordinator
// must outlive its submitted jobs (its destructor joins the worker pool first);
// open() must not be called while replay jobs are in flight — create a fresh
// coordinator per document.
class QueryCoordinator {
 public:
  QueryCoordinator(std::size_t worker_count, std::uint64_t replay_budget_bytes);
  ~QueryCoordinator();

  QueryCoordinator(const QueryCoordinator&) = delete;
  QueryCoordinator& operator=(const QueryCoordinator&) = delete;

  // Builds the scanline anchor index over `source` (shared ownership keeps it
  // alive for replay jobs). Returns false on failure. After a successful open
  // the document is "indexed".
  bool open(std::shared_ptr<const pnga::io::IByteSource> source,
            const pnga::png_reconstruction::ImageHeader& header,
            std::uint64_t anchor_interval_bytes);

  // Requests scanline `row` at `priority`. A not-yet-ready row has a replay job
  // submitted and returns kReplaying; a ready row returns kReady with its
  // unfiltered bytes; out-of-range or rejected rows return kError.
  ScanlineQueryResult query_scanline(std::uint64_t row, JobPriority priority);

  // Current status of `row` without submitting any work.
  ScanlineQueryResult status_for(std::uint64_t row) const;

  // Starts a new document generation: drops stale queued replays and resets all
  // per-row state to "indexed" (results of in-flight stale jobs are discarded
  // by the scheduler).
  void setDocumentGeneration(std::uint64_t generation);

  // Invoked from a worker thread whenever a row's status changes.
  void setStatusCallback(std::function<void(std::uint64_t row, QueryStatus)> cb);

  bool has_index() const noexcept { return has_index_; }
  std::uint64_t scanline_count() const noexcept { return scanline_count_; }
  std::size_t queued_replays() const noexcept { return scheduler_.queued_count(); }
  const ScanlineAnchorIndexResult& anchors() const noexcept { return anchors_; }

 private:
  struct RowState {
    QueryStatus status = QueryStatus::kIndexed;
    std::vector<std::byte> unfiltered;
    std::string error;
  };

  std::uint64_t row_reservation(std::uint64_t row) const noexcept;

  JobScheduler scheduler_;
  std::shared_ptr<const pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  std::unique_ptr<pnga::png_format::VirtualIDATStream> stream_;  // borrows index_
  pnga::png_reconstruction::ImageHeader header_;
  ScanlineAnchorIndexResult anchors_;
  std::function<void(std::uint64_t, QueryStatus)> on_status_;

  mutable std::mutex rows_mutex_;
  std::unordered_map<std::uint64_t, RowState> rows_;
  std::uint64_t generation_ = 0;
  bool has_index_ = false;
  std::uint64_t scanline_count_ = 0;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_QUERY_COORDINATOR_H
