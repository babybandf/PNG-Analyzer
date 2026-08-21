# pnga_deflate_index

Large-file random-access facilities over generic byte streams
(REPOSITORY_LAYOUT.md §5.6, ADR-0006).

## Responsibility

- Fast Deflate block index: a single inflate(Z_BLOCK) pass recording every
  block's type, BFINAL and input/output ranges (WP-401).
- Sparse checkpoints, bit alignment and 32 KiB dictionary snapshots (later WP).
- Bounded replay from a checkpoint to a requested scanline (later WP).

## Non-goals

- Token-level Deflate trace (`libs/deflate-trace`).
- Qt or libpng dependencies.

## Public targets

- `pnga_deflate_index` (alias `pnga::deflate_index`).

## Allowed dependencies

- `pnga_core`, `pnga_io`, zlib. Never Qt (ADR-0003). Input is a generic
  `IByteSource`; the virtual IDAT stream is adapted by callers, so IDAT data is
  never assumed contiguous.
