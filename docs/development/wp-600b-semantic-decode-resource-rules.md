# WP-600B — Semantic, Decode and Resource Rules

Status: **implemented** (2026-08-23)

## Frozen rule groups

The Qt-free validation layer now exposes three bounded checks:

- `validate_semantics`: exact 13-byte IHDR parsing; non-zero dimensions;
  PNG-approved bit-depth/color-type pairs; compression/filter method zero; and
  interlace method 0 or 1. Stable ids use the `ihdr_*` prefix.
- `validate_resources`: checked scanline arithmetic, a 2²⁸ per-axis safety
  limit and a 2³⁴-byte decoded scanline budget. Oversized input reports an issue
  without allocating image storage.
- `validate_decode_preflight`: IDAT presence and the two-byte zlib wrapper
  method/window/FCHECK/FDICT contract. It does not inflate; that remains the
  responsibility of `deflate-runtime` and the analysis engine.

Malformed inputs retain deterministic issue order and fixed PNG section
references. The checks consume the existing immutable `IByteSource` and
`ChunkIndex`, so they neither add Qt nor create a reverse dependency on a
decoder.

## Evidence

Focused Catch2 cases cover valid headers, invalid bit-depth/color-type and
interlace combinations, dimensions over the resource limit, missing IDAT and
an invalid zlib method. WP-600A integrity and the existing structural cases
remain in the same validation test executable.

