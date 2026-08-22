# pnga_validation

Structural, integrity, semantic and specification validation
(REPOSITORY_LAYOUT.md §5.11).

## Responsibility

- Structural, bounded CRC/Adler-32, semantic and specification validation
  rules. WP-600A checks Chunk CRCs without copying payloads and accepts a
  decoder-owned inflated span for Adler verification.
- WP-600B adds bounded IHDR semantic checks, checked image resource budgets and
  zlib/IDAT decode preflight without taking ownership of decoder work.
- Stable rule ids and deterministic reports.

## Non-goals

- Decoding, filtering or color transforms.
- Owning or copying chunk data.

## Public targets

- `pnga_validation` (alias `pnga::validation`).

## Allowed dependencies

- `pnga_core`, `pnga_png_format`, `pnga_trace_model`. Never Qt (ADR-0003).
