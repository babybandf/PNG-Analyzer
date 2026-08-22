# WP-603B — Decode/Reconstruction Fuzz Smoke

Status: **implemented** (2026-08-23)

The bounded fuzz executable now generates four deterministic PNG families:
RGBA8, interlaced RGB8, packed indexed 2-bit and 16-bit grayscale. Each case
replays stage filter formulas and the stored/fixed Deflate trace with a 1 MiB
output cap, then applies eight deterministic byte mutations. Mutated files may
return a structured error or partial stage result; they must not crash or
escape parser/decoder budgets.

No generated files are added to `tests/corpus`; the generator and mutation
seed are fixed in source for replay. This is smoke coverage for the later
sanitizer regression gate, not a claim of coverage-guided fuzz completeness.

