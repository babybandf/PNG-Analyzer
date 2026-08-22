# ADR-0004: Use A Unified Analysis And Selection Model

- Status: Accepted
- Date: 2026-08-20

## Context

Chunk, Deflate block, scanline and pixel views must navigate the same underlying data without each frontend reconstructing decoder state or inventing incompatible coordinates.

## Decision

Use backend-neutral `SemanticNode`, `StageArtifact`, `Selection` and `Provenance` models. A selection may carry semantic identity, physical spans, logical stream spans, stage, frame/pass/row and image coordinates. Provenance records permit one-to-many and many-to-one relationships and preserve reversible mappings where the format permits them.

Stable IDs identify objects within a document generation. GUI panels publish and consume the shared selection while preserving dimensions they do not understand.

## Consequences

- Chunk, Hex, stage and image panels can synchronize without decoder-specific objects.
- Decoder operations publish trace events and artifacts; the GUI does not recompute formulas.
- Image coordinates use global `x/y`, a pass-local `row`, and `pass=0` for
  non-interlaced images or `1..7` for Adam7. An absent channel means the whole
  pixel; channel, sample-byte and packed-bit selections are explicit. A
  `sample-byte` is an index within an 8/16-bit sample (the query enforces the
  format's byte count). A `packed-bit` uses a most-significant-bit offset and
  has PNG-legal length 1, 2 or 4; it is mutually exclusive with `sample-byte`.
- Equality, merge, serialization and stale-generation behavior require focused tests.
- The model is more explicit than view-specific structures, but it is the foundation for compare and first-difference workflows.
