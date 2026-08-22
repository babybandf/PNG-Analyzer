# WP-604A — Performance Corpus & Runner

Status: **implemented** (2026-08-23)

The repository now has a deterministic, generated performance corpus and a
machine-recording wrapper. No external PNG or generated binary is committed.

Run it with:

```text
python3 scripts/run_performance_corpus.py
```

The command builds `pnga_performance_runner`, records the fixed large-index
and pixel-provenance scenarios, and runs the existing GUI performance CTest
when Qt is configured. The default output is the ignored build artifact
`build/performance/wp-604a-latest.json`; use `--output` for another reviewed
destination.

## Record contract

The JSON field order is fixed as `schema`, `host`, `runner`, `ui`. The runner
schema is `pnga-performance-v1`; it contains corpus dimensions, PNG byte
counts, index/preview/provenance timings, random-row P50/P95, sample counts
and a checksum proving the measured results were consumed. Host fields are
limited to system, release, machine and Python version. No clock or locale
field is recorded.

The corpus runner is a measurement/reproducibility gate, not a threshold gate:
WP-604B will freeze acceptable values and detect regressions without changing
this corpus or hiding a slower baseline.

## Local evidence

On the local macOS arm64 runner (Qt 6.11.1), the generated corpus runner and
the GUI performance scenario pass. The record is intentionally kept under
`build/`; release and cross-platform baseline evidence remain future Gate
work.
