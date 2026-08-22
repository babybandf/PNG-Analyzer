# WP-5U6A — Async & Failure States

Status: **implemented** (2026-08-23)

`TraceInspectorStateMachine` is the Qt-free lifecycle contract for a bounded
trace request. It covers `empty → loading → replaying → ready/partial/error`,
explicit `cancelled`, and `stale generation`. Replacing a document clears the
bundle and changes the generation; a result from an older generation is
observable as stale but cannot overwrite the newer state.

`TraceInspectorBinding::publishState` applies one state and, when present, one
generation-consistent `TraceInspectorBundle` to the Block, Huffman Tables and
Decode Trace pages. The UI receives deterministic status text and errors while
the worker/orchestrator remains outside Qt widgets.

The state machine does not claim performance or cross-platform gates. Those
remain WP-5U6B/C work and keep their own corpus and manual evidence.

