# WP-603C — Sanitizer Regression Gate

Status: **implemented** (2026-08-23)

The fixed WP-603A parser/stream and WP-603B decode/reconstruction smoke cases
are now an explicit ASan/UBSan gate. `tests/fuzz/CMakeLists.txt` labels the
CTest entry as both `fuzz` and `sanitizer`, while
`scripts/run_sanitizer_fuzz_gate.py` provides one reproducible command that:

1. builds only `pnga_fuzz_smoke_tests` with the `asan` preset;
2. replays `[wp603a]` and `[wp603b]` independently; and
3. runs the registered `fuzz_parser_stream_smoke_tests` CTest entry.

## Replay records

```text
python3 scripts/run_sanitizer_fuzz_gate.py
build/asan/tests/fuzz/pnga_fuzz_smoke_tests "[wp603a]"
build/asan/tests/fuzz/pnga_fuzz_smoke_tests "[wp603b]"
```

The generated inputs remain in memory. Fixed seeds, dimensions and mutation
counts live in the test sources, so a failure can be replayed without adding
an opaque binary corpus or changing production code.

## Evidence

On the local macOS arm64 runner (Qt 6.11.1), the gate passed both tagged
replays and the CTest registration. The full development and ASan suites each
remain 31/31. This is sanitizer smoke/regression evidence; coverage-guided
fuzzing and native Windows/Linux sanitizer jobs remain release/CI work.
