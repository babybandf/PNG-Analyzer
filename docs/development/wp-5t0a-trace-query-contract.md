# WP-5T0A Trace Query Contract

Status: implemented  
Scope: Qt-free contract and bounded composition only; no worker, scheduler or
decoder changes.

## Result boundary

`pnga::analysis_engine::TraceQueryResult` is an immutable value result for one
inflated-output interval. It carries the document generation and original
`trace-model::Selection`, then exposes only associated Deflate blocks and the
bounded token/table data supplied by the caller.

The result preserves these relationships:

- block `BFINAL/BTYPE`, logical input bit range and inflated output range;
- token kind, exact input bits, output range, literal or length/distance fields,
  overlap-safe match source ranges and associated block id;
- code-length, literal/length and distance Huffman table entries;
- logical Deflate and physical file bit provenance, including spans split at
  Virtual IDAT segment boundaries.

The contract never concatenates IDAT payloads and does not retain an implicit
whole-file token trace. The caller supplies an explicit `max_tokens` budget.
When that budget or an upstream artifact failure limits the answer, verified
ranges remain available and the status is `partial`.

## Stable states

The serialized status vocabulary is fixed and locale-independent:

`not indexed`, `replaying`, `ready`, `partial`, `error`, `cancelled`.

`compose_trace_query()` accepts already-produced block and token artifacts. It
returns `error` for invalid output ranges or a zero token budget, `partial`
when either artifact is unavailable, and `ready` only when the requested range
was composed without truncation. No thread is started by this API.

## Serialization

`serialize_trace_query()` emits `trace-query-v1` with fixed field order,
decimal numeric fields, stable enum text, deterministic vector order and
percent-escaped diagnostic text. It is intended for golden tests, logs and
future cache keys; it contains every result field and no locale/clock data.

Focused tests cover a ready result, bounded partial results, invalid requests,
Stored/Fixed/Dynamic-compatible token output, multiple IDAT physical spans and
deterministic serialization.
