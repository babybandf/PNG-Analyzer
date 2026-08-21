# pnga_deflate_runtime

Fast production Inflate built on public zlib APIs (REPOSITORY_LAYOUT.md §5.5).

## Responsibility

- Streaming Inflate wrapper.
- zlib wrapper/header and Adler-32 status reporting.
- Cancellation, bounded output and error translation (later WP).
- State-export hooks required by the index module, using supported APIs only.

## Non-goals

- Token-level Deflate traces (libs/deflate-trace owns those).
- PNG scanline semantics or filter logic.

## Public targets

- `pnga_deflate_runtime` (alias `pnga::deflate_runtime`).

## Allowed dependencies

- `pnga_core`, `pnga_io`, zlib. Never Qt (ADR-0003).
