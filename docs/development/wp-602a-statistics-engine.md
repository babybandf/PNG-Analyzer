# WP-602A — Statistics Engine

Status: **scope and first implementation frozen** (2026-08-23)

## Decision

Statistics is an optional M6 capability and is not part of the first single-
file v1 acceptance surface. Its first increment is a Qt-free aggregation
library, `pnga_statistics`, under the canonical `libs/statistics/` path. The
library receives backend-neutral scalar samples; composition code adapts
`ChunkIndex`, reconstruction/filter rows and Deflate block/token results at
the analysis boundary. This keeps the dependency graph acyclic and prevents
the statistics module from parsing PNG or decoding Deflate.

The first increment covers:

- Chunk counts and payload-byte totals, bucketed by the four-byte Chunk type;
- PNG filter row and data-byte totals for filter types 0–4, plus invalid-row
  accounting;
- Stored/Fixed/Dynamic Deflate block counts, compressed bit totals and output
  byte totals;
- literal/match/EOB token counts and input/output totals;
- sorted length and distance value histograms for match tokens;
- integer compressed/inflated totals and a locale-independent per-mille rate.

JSON/CSV export, selection navigation and GUI presentation remain WP-602B.
No full-file payload or decoded image is copied by this engine.

## Input and output invariants

- `StatisticsInput` spans are borrowed and consumed synchronously; the caller
  owns their backing storage for the duration of `collect()`.
- Every counter uses checked addition. Overflow returns `overflow` and keeps
  the validated prefix instead of wrapping.
- Bucket counts are bounded by `StatisticsLimits`; cancellation returns the
  validated prefix with stable `cancelled` status.
- Chunk buckets and value histograms are sorted deterministically. Fixed
  filter/block/token arrays use their protocol order.
- Invalid Chunk type length, block/token kind or zero length/distance match
  returns `invalid_input`; it never reads outside a sample span.

## Verification

`statistics_engine_tests` covers deterministic totals and bucket ordering,
compression rate, cancellation with partial preservation, invalid inputs,
bucket budgets and arithmetic overflow. The target links only `pnga_core` and
`pnga_trace_model`; it contains no Qt, file I/O or decoder dependency.

The next bounded increment is to adapt immutable analysis results into these
sample spans and decide whether WP-602B should enter the v1 surface. That
increment must preserve the same limits and cancellation contract.
