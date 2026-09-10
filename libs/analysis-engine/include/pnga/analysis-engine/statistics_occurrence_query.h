#ifndef PNGA_ANALYSIS_ENGINE_STATISTICS_OCCURRENCE_QUERY_H
#define PNGA_ANALYSIS_ENGINE_STATISTICS_OCCURRENCE_QUERY_H

// WP-602F: bounded statistics occurrence navigation (WP-602F review ruling
// R12). Chunk, filter and block domains resolve directly from the existing
// indexes; token, length and distance domains run a bounded scalar token
// scan that retains no occurrence list — first stops on the first match,
// next skips output offsets at or before the cursor and previous retains
// only the last matching scalar before the cursor. Every matched fact maps
// to the existing typed Selection with every cross-IDAT physical span. A
// budget-exhausted scan returns a verified partial with the searched range.

#include <cstdint>
#include <string>

#include <pnga/analysis-engine/statistics_view.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/trace-model/selection.h>

namespace pnga::analysis_engine {

class CancellationToken;
struct AnalysisTarget;
struct StageSet;

enum class OccurrenceStatus { kReady, kPartial, kNotFound, kCancelled, kError };

struct StatisticsOccurrenceResult {
  OccurrenceStatus status = OccurrenceStatus::kError;
  std::string error;
  std::uint64_t generation = 0;
  pnga::trace_model::Selection selection;
  // Verified prefix searched before a budget limit or a match stopped the
  // scan: tokens delivered and DEFLATE input bytes consumed (rounded up).
  std::uint64_t searched_input_bytes = 0;
  std::uint64_t searched_tokens = 0;
};

// Resolves one occurrence of the bucket identified by
// `request.domain`/`request.key`. `source` is the whole file and `chunks`
// its index; the virtual IDAT stream is built internally without
// concatenation. `stages` backs the filter domain, `blocks` the block and
// token domains; a missing dependency fails with a stable error instead of
// guessing. The generation is echoed unchanged so the caller can reject
// stale results before publishing them through the selection bus.
StatisticsOccurrenceResult query_statistics_occurrence(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& chunks, const StageSet* stages,
    const pnga::deflate_index::BlockIndexResult* blocks,
    const StatisticsNavigationRequest& request,
    const CancellationToken* cancellation);

// Frame-scoped occurrence resolution (WP-APNG-INSPECT contract C7): the
// stream under navigation is the target's frame stream, so chunk-domain
// results cover only the frame's own data chunks (fcTL stays unindexed and
// is reported honestly as partial), while block/filter/token domains map
// through the frame stream. Any returned image coordinate carries
// target.key.identity and canvas-global coordinates; File chunk range
// queries are not converted. Whole-file chunk navigation keeps using the
// original query_statistics_occurrence path.
StatisticsOccurrenceResult query_frame_statistics_occurrence(
    const AnalysisTarget& target, const StageSet* stages,
    const pnga::deflate_index::BlockIndexResult* blocks,
    const StatisticsNavigationRequest& request,
    const CancellationToken* cancellation);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STATISTICS_OCCURRENCE_QUERY_H
