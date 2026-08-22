# pnga_png_format

Physical and logical PNG structure (REPOSITORY_LAYOUT.md §5.3).

## Responsibility

- PNG signature and Chunk envelope parsing.
- Chunk type flags, field decoding, order metadata and bounded CRC calculation.
- `VirtualIDATStream` and logical-to-physical span mapping (later WPs).
- Static PNG and APNG Chunk data structures.

## Non-goals

- Decoding, filtering or color transforms.
- Owning or copying chunk data (indexes borrow `ByteSource` ranges, ADR-0005).

## Public targets

- `pnga_png_format` (alias `pnga::png_format`).

## Allowed dependencies

- `pnga_core`, `pnga_io`, approved CRC provider. Never Qt (ADR-0003).
