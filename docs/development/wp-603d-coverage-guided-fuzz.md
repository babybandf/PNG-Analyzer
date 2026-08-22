# WP-603D — Coverage-guided fuzz evidence

Status: **optional harness and runner added; runtime evidence depends on the
compiler toolchain** (2026-08-23)

The repository now has an opt-in `pnga_coverage_fuzz` LLVM libFuzzer harness
for bounded Chunk/Virtual IDAT/validation/zlib-wrapper inputs. It is isolated
from the normal dev and ASan presets because the libFuzzer runtime is supplied
by the compiler and is not an approved third-party dependency. The
`coverage-fuzz` preset disables Qt and enables the compiler sanitizer flags.

Run:

```text
python3 scripts/run_coverage_fuzz.py --seconds 30
```

The runner probes `-fsanitize=fuzzer` first. A missing runtime produces an
explicit `NOT_CONFIGURED` JSON report and exit status 0; a configured runtime
builds and runs the harness with generated local seeds, and only then reports
`PASS`. It never claims coverage completeness, native platform coverage or a
fixed crash corpus. ASan/UBSan deterministic smoke remains covered by
`scripts/run_sanitizer_fuzz_gate.py`.
