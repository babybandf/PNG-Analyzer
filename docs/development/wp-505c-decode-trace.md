# WP-505C — Decode Trace

Status: **implemented** (2026-08-22)

## Scope

`DecodeTraceInspectorView` projects the bounded token rows from
`TraceQueryResult` into a deterministic, step-oriented explanation:

- literal, length/distance match and end-of-block paths retain their input-bit
  and inflated-output half-open ranges;
- match rows expose RFC 1951 length/distance base values, extra-bit counts and
  decoded extra values;
- match source intervals are copied from the trace, including overlap-safe root
  origins; and
- a selected token or output byte marks the owning step and preserves the
  selected output position.

The Qt `DEFLATE / Decode Trace` tab renders these rows and emits bounded Hex
and DEFLATE range signals. It does not read files, run Inflate or infer source
origins. Full physical-file mapping and explicit Trace-to-Original-Literal
recursion remain separate, user-triggered work for the M5 Gate follow-up.

## Verification

Qt-free tests cover literal/match arithmetic, EOB and partial status. The Qt
test covers match rendering, overlap source text and Hex navigation. Dev and
ASan suites include the new inspector and main-window tab contract.

