#ifndef PNGA_ANALYSIS_ENGINE_STATISTICS_ADAPTER_H
#define PNGA_ANALYSIS_ENGINE_STATISTICS_ADAPTER_H

// WP-602A: adapt immutable analysis results to the backend-neutral statistics
// engine. This boundary performs no parsing, decoding or payload copying.

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/statistics/statistics.h>

namespace pnga::analysis_engine {

struct StatisticsSources {
  const pnga::png_format::ChunkIndex* chunks = nullptr;
  const StageSet* stages = nullptr;
  const pnga::deflate_index::BlockIndexResult* blocks = nullptr;
  const pnga::deflate_trace::TokenDecodeResult* tokens = nullptr;
};

// Projects the supplied immutable results into scalar samples and invokes
// pnga::statistics::collect(). Missing sources are allowed, so callers can
// report partial analysis without inventing zero-valued evidence. Source
// errors and malformed ranges are returned as stable invalid_input status.
pnga::statistics::StatisticsSnapshot collect_statistics(
    const StatisticsSources& sources,
    pnga::statistics::StatisticsLimits limits = {},
    pnga::statistics::CancelPredicate should_cancel = {});

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STATISTICS_ADAPTER_H
