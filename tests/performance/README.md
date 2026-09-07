# WP-604A performance corpus

`pnga_performance_runner` generates the same bounded in-memory corpus on every
run; it does not download or write PNG fixtures. The corpus has three fixed
scenarios:

| Scenario | Input | Measurements |
|---|---|---|
| `large-index` | 1024×768 RGBA8, non-interlaced, deterministic all-None filters | PNG size, Chunk index, fast-index, in-memory reopen, 64 random scanline restores (P50/P95) |
| `pixel-provenance` | 8×5 RGBA8, non-interlaced, deterministic rotating filters | stage/first-preview, 16 deterministic pixel provenance queries (P50/P95) |
| `compression-inspector` | WP-607C `perf-large-rgba8` (49 Stored blocks) plus `trace-fixed-nonoverlap` and `trace-dynamic-overlap-repeats` | Fast Index projection, bounded 4,096-token Deep Trace query, three inspector model publications, first visible rows, 200 deterministic visible-row reads, checksum |

Rows use the fixed sequence `((i * 2654435761) + 17) mod scanline_count`.
Pixel coordinates and channels are fixed in the runner source. Durations are
unsigned microseconds; the runner checks successful completion but does not
enforce thresholds by default. `thresholds-v1.json` freezes the WP-604B
maximum values; the explicit gate command is:

```text
python3 scripts/run_performance_corpus.py --enforce-thresholds
```

The values are deliberately a fixed local gate with headroom for normal
machine noise, not a claim about every platform's release performance.

## compression-inspector scenario (WP-5U12F)

All measurements are Qt-free and run over the WP-607C controlled fixtures
built in memory. The replay budgets mirror the production bounded trace path,
so the measured pipeline is the published behavior:

- `fast_index_us` — the complete Blocks model publication on open:
  `index_blocks` (64 MiB output budget) plus `build_fast_compression_index`
  over `perf-large-rgba8`; the row count must equal the corpus block facts.
- `trace_query_4096_us` — one bounded Deep Trace query on a 65,536-byte
  window: `decode_stored_and_fixed` (window plus the production 64 KiB replay
  look-ahead) plus `compose_trace_query` with the WP-5U13 4,096-token budget;
  the result must be partial, truncated and carry exactly 4,096 tokens.
- `huffman_model_us` — the Huffman model publications over the fixed and
  dynamic corpus blocks; the fixed literal/length table must be complete at
  its maximum bounded size (288 entries, RFC 1951 §3.2.6).
- `decode_trace_model_us` — the Decode Trace model publications over the
  bounded 4,096-token query and the dynamic corpus result; the bounded
  window's output ranges must tile `[0, 4096)` exactly.
- `first_visible_rows_us` — deterministic on-demand formatting of the first
  32 visible rows of the Blocks, Huffman and Decode Trace models.
- `visible_row_reads_us` — 200 deterministic visible-row reads per model
  using the fixed sequence above.
- `checksum` — accumulated read facts (recorded, not thresholded).

## statistics scenario (WP-602H)

All measurements are Qt-free and run the real lazy whole-document pipeline
(`analyze_source` + `collect_document_statistics` over the WP-607C
`perf-large-rgba8` fixture):

- `fast_sections_us` — time to the first token-scan progress publication, an
  upper bound of the collector's fast-sections phase (identity, overview,
  chunks, filters, blocks) including the <= 100 ms production progress
  throttle slack.
- `whole_document_us` — the complete collection duration including the full
  streaming token scan over the virtual IDAT stream (never a payload copy).
- `token_count` — collected whole-document tokens (recorded, not
  thresholded); must be positive.
- `peak_retained_token_records` — non-time invariant, enforced by the runner:
  a scalar `scan_tokens` pass over the same logical stream must retain at
  most one token record.
- `max_working_bytes` — non-time invariant, enforced by the runner: the
  declared background working memory stays within the frozen 64 MiB cap.
- `view_projection_us` — `build_statistics_view` over the collected snapshot.
- `serializer_us` — both shared serializers (JSON and CSV) over the
  collected snapshot; both must succeed.
- `checksum` — accumulated view row and serialized byte counts (recorded,
  not thresholded).

Screenshot capture is deliberately not measured; visual evidence belongs to
the GUI product gate. The GUI-side response gate is the existing
`gui_trace_inspector_performance_tests` CTest entry. The wrapper records it as
`passed` when Qt is configured, or `not-configured` on a Qt-free build.

## statistics-bounded-blocks scenario (WP-602 quality fix)

Stress scenario over the frozen quality-audit structure: a valid 1x1 gray8
PNG whose zlib stream holds 1,500,000 empty stored blocks plus one final
two-byte block (two inflated bytes, ~7.5 MB compressed). The collector must
stop with a verified block prefix and honest statuses instead of retaining
the whole index:

- `whole_document_us` — the complete collection duration; the only
  thresholded metric.
- `verified_block_count` — the retained verified block prefix (runner
  enforced: positive and within the 2^20 sample budget); the block and
  overview sections stay `budget_exceeded`/incomplete with the totals pair
  absent, and the value equals a direct `index_blocks_bounded` run with the
  collector's budgets.
- `retained_capacity_bytes` — the real block vector capacity (allocation
  peaks included) of that direct bounded scan, runner enforced to stay
  within the 32 MiB half-cap share of the declared 64 MiB working memory.
- `read_bytes` — the highest byte offset the collection actually read via a
  counting byte source (runner enforced: within the file size). The
  pre-scan and the token replay are separate passes over the same window;
  the record shows their total read work, not a per-phase split.
- `cancel_trigger_reads` / `cancel_total_reads` — a second instrumented run
  arms the cancellation after 130 reads (deterministically inside the block
  scan: ~115 fingerprint windows precede it) and the scan must stop within
  two additional reads (one refill unit plus one block-header read), no
  wall-clock slack involved. The cancelled run must report the frozen
  cancelled overview with no totals.
- `process_rss_peak_kib` — auxiliary process peak RSS in KiB (platform units
  normalized: getrusage `ru_maxrss`, or `GetProcessMemoryInfo` peak working
  set on Windows); recorded only, never thresholded — the
  declared 64 MiB cap governs the job's own working memory, not the whole
  process. Known coverage note: the CLI composition pre-builds a full
  StageSet (`analyze_source`) before the collector runs; that preprocessing
  allocation is outside the declared collector working memory and is
  deliberately not claimed as covered.
