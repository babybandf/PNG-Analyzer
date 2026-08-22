# WP-604B — Performance Threshold Gate

Status: **implemented** (2026-08-23)

`tests/performance/thresholds-v1.json` freezes maximum microsecond values for
the WP-604A generated corpus. The existing runner gains an explicit
`--enforce-thresholds` mode so a normal measurement remains useful for
exploration while CI/release checks fail on a regression.

Run the gate with:

```text
python3 scripts/run_performance_corpus.py --enforce-thresholds
```

The gate verifies that every measured scenario and configured metric exists,
that values are non-negative integers, and that each value stays below its
fixed maximum. It writes the measurement record before reporting failure, so a
regression is inspectable under `build/performance/` without changing the
baseline. GUI performance remains represented by the existing
`gui_trace_inspector_performance_tests` status; native window-system thresholds
remain release/CI work.

The thresholds have headroom over the local macOS arm64 dev measurements and
are intended to catch order-of-magnitude regressions. They are not silently
updated from a slower run; any threshold change requires a reviewed plan/PR
change.
