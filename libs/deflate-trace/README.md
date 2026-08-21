# pnga_deflate_trace

Deep Deflate trace facilities (REPOSITORY_LAYOUT.md §5.7, ADR-0006).

## Responsibility

- Transparent zlib wrapper trace: CMF/FLG/FCHECK/FDICT/FLEVEL/DICTID and the
  trailing Adler-32, each as a trace-model field node with a bit span (WP-500).
- Stored/fixed/dynamic block token traces (later WP).
- LZ window and token output ranges (later WP).

## Non-goals

- Fast block indexing / random access (`libs/deflate-index`).
- Inflate itself (`libs/deflate-runtime`).
- Qt or libpng dependencies.

## Public targets

- `pnga_deflate_trace` (alias `pnga::deflate_trace`).

## Allowed dependencies

- `pnga_core`, `pnga_io`, `pnga_trace_model`. Never Qt (ADR-0003). The module
  is zlib-free: Adler-32 and Deflate-data verification are cross-checked by
  callers with standard zlib through `libs/deflate-runtime`.
