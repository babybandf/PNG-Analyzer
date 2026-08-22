# WP-5U6B — UI Performance Gate

Status: **implemented** (2026-08-23)

## Bounded rendering contract

- Block Inspector renders at most 2,048 rows.
- Huffman Tables and Decode Trace render at most 4,096 rows each.
- Hex highlights retain at most 4,096 spans.
- When a result exceeds a cap, the widget keeps a deterministic `truncated`
  row; it never expands a second unbounded GUI model or copies the source file.

## Fixed local evidence

Runner: macOS arm64, Qt 6.11.1, offscreen platform, generated in-memory
10,000-block/10,000-token input. The performance regression test renders the
same data twice to exercise cold and hot paths and asserts each under 2,000 ms.
One observed run:

```text
trace inspector render cold_ms=23 hot_ms=27
```

The same test verifies the three row caps and 4,096 Hex highlight cap. Dev and
ASan suites both pass 29/29. This is a fixed local regression baseline, not a
cross-platform performance claim; WP-5U6C retains the platform matrix and
release thresholds.

