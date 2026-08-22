# WP-600A — Integrity Rules

Status: **implemented** (2026-08-23)

## Frozen contract

`pnga::validation::validate_integrity` is Qt-free and consumes an immutable
`IByteSource` plus a previously built `ChunkIndex`:

- every indexed Chunk CRC covers exactly `type || data` and is calculated in a
  fixed 64 KiB read window;
- an unreadable indexed span produces `chunk_integrity_unreadable`, while a
  value mismatch produces `chunk_crc_mismatch` at the physical CRC offset;
- a logical IDAT stream shorter than the two-byte zlib header plus four-byte
  Adler trailer produces `idat_adler_truncated`;
- callers that already own decoder output may pass a borrowed immutable
  inflated span to check the trailer, producing stable
  `idat_adler_unreadable`, `idat_adler_input_too_large` or
  `idat_adler_mismatch` rules as appropriate.

The validation layer does not inflate, concatenate IDAT payloads or retain a
second copy. Adler offsets are mapped back to physical IDAT spans. All issue
ids, messages, severities and spec references are fixed strings, independent
of locale and execution order.

## Evidence

The focused Catch2 suite covers valid CRCs, a CRC mismatch and physical offset,
IDAT trailer truncation, valid/mismatched Adler values and a 70 KiB Chunk to
exercise the bounded CRC window. The existing structural validation cases
remain in the same test executable.

