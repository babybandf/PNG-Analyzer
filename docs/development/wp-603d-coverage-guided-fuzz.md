# WP-603D — Coverage-guided fuzz evidence

Status: **Ubuntu LLVM/libFuzzer runtime evidence passing in CI** (2026-08-23)

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

CI evidence: the Ubuntu `coverage-fuzz` job in CI run `32610155310` installed
the pinned LLVM/libFuzzer runtime, built `pnga_coverage_fuzz`, ran the bounded
10-second campaign, and uploaded a `PASS` `build/coverage-fuzz/evidence.json`
artifact. The same run's Ubuntu ASan, Windows, and macOS build jobs also passed.
The local Apple Command Line Tools environment still reports `NOT_CONFIGURED`
because its libFuzzer runtime is absent; this does not weaken the CI gate.
