# Trace semantics

This note is the compact vocabulary shared by the Qt-free analysis model, CLI
reports and GUI inspectors. It complements [ADR-0006](../adr/ADR-0006-fast-index-on-demand-deep-trace.md)
and the [Trace Query Contract](wp-5t0a-trace-query-contract.md).

## Coordinate and stage chain

One locked image coordinate can carry image `(x,y)`, optional channel/sample,
stage, scanline/pass, Chunk and one-or-more logical/physical spans. The stages
are related but not interchangeable:

```text
native sample → reconstructed bytes → filtered scanline bytes
              → inflated DEFLATE output → token/block bit ranges
              → Virtual IDAT logical bytes → physical file spans
```

For filtered rows, the filter byte is part of the inflated scanline. The
reverse-filter formula may depend on left, previous-row and upper-left bytes;
therefore one sample can legitimately have fan-in from several filtered and
token ranges. A DEFLATE match can also point to an overlapping source range.

## IDAT and bit provenance

All IDAT payloads form a logical Virtual IDAT stream backed by physical
segments. The implementation maps logical ranges to physical spans on demand;
it does not allocate a second buffer containing the complete IDAT payload.
Deflate input bit ranges are half-open and retain their block/token context.
Physical spans are byte/bit aligned records with checked offset-plus-length
arithmetic.

## Fast index versus Deep Trace

The fast path records Chunk, IDAT and Deflate block/access information and
scanline anchors. It does not retain every token. A Deep Trace request replays
only the selected bounded output interval or block, then publishes an immutable
result for the requested document generation.

Stable trace states are `not indexed`, `replaying`, `ready`, `partial`,
`error` and `cancelled`. `partial` means verified ranges remain available but
the requested budget or upstream artifact was insufficient. `cancelled` and a
stale generation must never overwrite a newer selection.

## Inspector responsibilities

The Block Inspector presents BFINAL/BTYPE, Deflate input bits, inflated output
bytes and physical IDAT provenance. Huffman Tables presents canonical entries
and their code-length provenance. Decode Trace presents literal, match and
end-of-block steps, including length/distance extra bits and overlap-safe match
source ranges. None of these Qt widgets parses PNG, runs Inflate or invents a
second provenance mapping.

## Deterministic evidence

Serialized trace and validation reports use fixed field order, decimal numbers,
stable enum/issue text and no locale or clock. Timing records are separate
performance artifacts and must not be used to rewrite trace or JSON goldens.
