# WP-5T0B On-demand Trace Orchestration

Status: implemented  
Scope: Qt-free orchestration; no decoder changes and no default whole-file
token retention.

## Boundary

`TraceOrchestrator` owns one document's immutable source, Virtual IDAT segment
table and fast Deflate block index. A `TraceOrchestrationRequest` supplies the
document generation, Selection, inflated output interval, token limit and
explicit replay/output budget.

The worker performs a bounded token replay, composes the result through
`compose_trace_query()` and stores it only when the request generation still
matches the current document. `JobScheduler` drops queued stale jobs and
suppresses running stale/cancelled results before the callback.

## State and safety rules

- `open()` builds the block index synchronously and is rejected while an older
  replay is queued or running; source ownership remains shared with the job.
- Requests with no index, a stale generation, a zero budget or an out-of-range
  output interval are rejected before submission.
- `trace_output_budget_bytes` is both the scheduler reservation and the decoder
  output cap; `max_tokens` bounds the published result.
- Cancellation is cooperative. Cancelled work never invokes the result
  callback, and completed values are removed from the bounded pending map.
- Callbacks run on worker threads and must not re-enter the orchestrator.

The orchestrator does not concatenate IDAT payloads or modify the Deflate
decoder. It is the handoff point for the later Qt adapter and WP-505 UI.
