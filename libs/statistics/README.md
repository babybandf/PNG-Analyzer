# pnga_statistics

Bounded, deterministic and Qt-free Chunk, PNG filter, Deflate block/token and
length/distance aggregation for WP-602A.

## Responsibility

- Consume backend-neutral scalar samples synchronously.
- Preserve partial validated totals when cancellation, overflow or a bucket
  budget stops collection.
- Emit sorted Chunk and length/distance buckets and fixed-order filter/block/
  token buckets.
- Keep compression totals as integers; callers can format the per-mille rate
  without locale-dependent floating-point text.
- `FrameStatistics` and the independent `pnga.frame-statistics` v1 JSON/CSV
  export schema (WP-APNG-INSPECT): frame identity, geometry, payload/overhead/
  inflated byte accounting and the shared section encoding. Static v1 output
  (`pnga.statistics`) is byte-identical and the section emitters are shared
  verbatim (serialization_sections.*). Static-image input is rejected.

## Non-goals

- Parsing PNG, decoding Deflate, reading files or copying payloads.
- Qt UI, JSON/CSV export or selection navigation (WP-602B).
- Owning borrowed sample storage or starting worker threads.

## Public target

- `pnga_statistics` (alias `pnga::statistics`)

## Allowed dependencies

- `pnga_core`, `pnga_trace_model` only. Never Qt.
