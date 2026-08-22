# M5 Trace Gate — bounded inspector binding

Status: **implemented for the bounded trace binding slice** (2026-08-23)

The gate now has one Qt-free projection boundary:

1. `TraceOrchestrator` publishes one immutable `TraceQueryResult` for the
   requested generation.
2. `build_trace_inspector_bundle` projects that result once into Block,
   Huffman Tables and Decode Trace views with the same generation.
3. `TraceInspectorBinding` publishes all three views at the Qt boundary in one
   operation; stale UI callbacks can be rejected by generation before publish.
4. Hex/DEFLATE navigation ranges are explicit and bounded. The optional Trace
   to Original Literal walk is user-triggered, cycle-safe and constrained by
   depth and visited-node budgets.

The gate does not claim the later cross-platform/performance/fuzz gates are
closed. Physical Hex mapping for logical bit ranges and the full manual UI
matrix remain in WP-5U6A/B/C and the M5 release evidence package.

Verification includes the real orchestrator ready callback, bundle generation
consistency, stale/cancel behavior from WP-5T0B, the Qt binding test, dev and
ASan CTest suites, differential tests and repository/dependency audits.

