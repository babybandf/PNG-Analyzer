# pnga_png_reconstruction

Deterministic PNG reconstruction after Inflate (REPOSITORY_LAYOUT.md §5.8).

## Responsibility

- Pass and scanline splitting.
- All five reverse filters (later WP).
- Adam7 geometry and placement.
- Packed sample extraction and native samples (later WP).
- Palette, transparency and approved delivery transforms.

## Non-goals

- Deflate/Inflate (libs/deflate-runtime owns that).
- Qt or libpng dependencies.

## Public targets

- `pnga_png_reconstruction` (alias `pnga::png_reconstruction`).

## Allowed dependencies

- `pnga_core`, `pnga_trace_model`. Never Qt (ADR-0003).
