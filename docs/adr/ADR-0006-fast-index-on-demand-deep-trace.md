# ADR-0006: Combine Fast Indexing With On-Demand Deep Trace

- Status: Accepted
- Date: 2026-08-20

## Context

Persisting every byte and token at every decode stage would multiply memory use and delay first interaction. Most users initially need structure, preview and coarse block/row navigation; detailed token and formula events are local queries.

## Decision

Use a tiered analysis policy:

- Fast paths build structural indexes, hashes, statistics and bounded checkpoints with public zlib APIs.
- Row-level artifacts are materialized for selected or nearby scanlines as needed.
- Deep Trace replays only a selected block, row or provenance region and emits detailed Huffman, token, filter or pixel events.

Artifacts are immutable, cacheable and evictable under an explicit memory budget. Missing capabilities are exposed as `not indexed`, `replaying`, `ready` or `error` states.

## Consequences

- Large files become navigable before all details are materialized.
- Replay and checkpoint policy becomes a first-class performance concern.
- The default path cannot retain an unbounded whole-file token event stream.
- Results must be deterministic whether produced during initial decode or replay.
