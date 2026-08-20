# pnga_io

Input access and file identity (REPOSITORY_LAYOUT.md §5.2).

## Responsibility

- `ByteSource` and bounded random-access read interfaces.
- Regular-file (mmap), memory and test implementations.
- mmap / windowed-mmap policy.
- File fingerprints, modification detection and asynchronous read primitives (later WPs).

## Non-goals

- Understanding PNG Chunk types or Deflate syntax.
- Caching or parsing file contents.

## Public targets

- `pnga_io` (alias `pnga::io`).

## Allowed dependencies

- `pnga_core`, platform file APIs. Never Qt (ADR-0003).

## Lifetime contract

`ByteView` results are borrowed from the owning `IByteSource` and are valid
only until that source is destroyed. `read()` copies and is always safe.
