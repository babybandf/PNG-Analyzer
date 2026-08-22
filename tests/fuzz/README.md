# Parser/stream fuzz smoke

WP-603A runs 512 deterministic xorshift inputs, each at most 4 KiB, through
`ChunkIndex`, the Virtual IDAT stream, the shared validation composition and
the zlib-wrapper trace parser. The fixed seed (`0x603a2026`) makes a failure
replayable from the test binary without writing generated files into the
corpus. The harness checks bounded logical reads and wrapper size invariants;
it is a smoke/foundation target, not a claim of coverage-guided fuzzing.

The same executable also runs WP-603B over generated RGB/RGBA, packed indexed,
16-bit grayscale and Adam7 images. It replays filter formulas and token trace
output, then applies eight bounded byte mutations per image; mutations may
yield errors or partial results but must not escape parser budgets.
