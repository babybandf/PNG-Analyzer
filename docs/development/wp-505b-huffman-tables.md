# WP-505B — Huffman Tables

Status: **implemented** (2026-08-22)

## Scope

`HuffmanInspectorView` is the Qt-free projection of the bounded
`TraceQueryResult` table summaries. It presents the RFC build order and keeps
the decoder's canonical entries and provenance bit windows unchanged:

- Stored blocks expose the LEN/NLEN pair.
- Fixed blocks expose the predefined literal/length (288) and distance (32)
  table capacities without duplicating the decoder's table construction.
- Dynamic blocks preserve code-length, literal/length and distance table order,
  entry symbols, bit lengths, canonical codes and code-length provenance.
- A selected literal token carries its input bit range and marks the matching
  literal/length entry when that entry is present in the bounded result.

The Qt `DEFLATE / Huffman Tables` tab only formats this immutable projection.
It does not parse a stream, build a canonical table, or perform file I/O. The
next work package will add the per-token Decode Trace view and its explicit
source navigation.

## Verification

Qt-free tests cover build order, selected literal mapping and Stored/Fixed
capability rows. A Qt test covers the dynamic entry presentation and selected
bit annotation. The projection retains query generation and deterministic
serialization for cache/golden use.

