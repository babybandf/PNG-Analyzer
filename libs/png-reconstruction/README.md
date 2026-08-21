# pnga_png_reconstruction

Deterministic PNG reconstruction after Inflate (REPOSITORY_LAYOUT.md §5.8).

## Responsibility

- Pass and scanline splitting.
- All five reverse filters.
- Adam7 geometry, pass unfiltering and placement (WP-303).
- Packed sample extraction and native samples (WP-304).
- Palette, transparency and approved delivery transforms.

## Non-goals

- Deflate/Inflate (libs/deflate-runtime owns that).
- Qt or libpng dependencies.

## Public targets

- `pnga_png_reconstruction` (alias `pnga::png_reconstruction`).

## Allowed dependencies

- `pnga_core`, `pnga_trace_model`. Never Qt (ADR-0003).
