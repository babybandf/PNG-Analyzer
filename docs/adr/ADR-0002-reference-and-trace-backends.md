# ADR-0002: Separate Reference And Trace Backends

- Status: Accepted
- Date: 2026-08-20

## Context

libpng provides mature production decoding but its public API does not expose every physical Chunk span, Deflate token, Huffman table or reverse-filter intermediate required by the analyzer.

## Decision

Maintain two independently useful decode paths:

- `LibpngBackend` uses stable public libpng APIs and supplies reference metadata, rows, final pixels and differential evidence.
- `TraceBackend` owns observable PNG reconstruction and, on demand, instrumented Deflate decoding with provenance.

Backends declare capabilities explicitly. Missing stages are reported as unsupported rather than represented by fabricated or empty artifacts.

## Consequences

- Trace output can be checked byte-for-byte against an independent production decoder.
- The project avoids a mandatory private libpng fork and private-structure coupling.
- Shared artifacts and comparison contracts must remain backend-neutral.
- A future instrumented libpng backend is optional and requires a separate version-locked decision.
