# WP-604A performance corpus

`pnga_performance_runner` generates the same bounded in-memory corpus on every
run; it does not download or write PNG fixtures. The corpus has two fixed
scenarios:

| Scenario | Input | Measurements |
|---|---|---|
| `large-index` | 1024×768 RGBA8, non-interlaced, deterministic all-None filters | PNG size, Chunk index, fast-index, in-memory reopen, 64 random scanline restores (P50/P95) |
| `pixel-provenance` | 8×5 RGBA8, non-interlaced, deterministic rotating filters | stage/first-preview, 16 deterministic pixel provenance queries (P50/P95) |

Rows use the fixed sequence `((i * 2654435761) + 17) mod scanline_count`.
Pixel coordinates and channels are fixed in the runner source. Durations are
unsigned microseconds; the runner checks successful completion but does not
enforce thresholds by default. `thresholds-v1.json` freezes the WP-604B
maximum values; the explicit gate command is:

```text
python3 scripts/run_performance_corpus.py --enforce-thresholds
```

The values are deliberately a fixed local gate with headroom for normal
machine noise, not a claim about every platform's release performance.

The UI scenario is the existing `gui_trace_inspector_performance_tests` CTest
entry. The wrapper records it as `passed` when Qt is configured, or
`not-configured` on a Qt-free build.
