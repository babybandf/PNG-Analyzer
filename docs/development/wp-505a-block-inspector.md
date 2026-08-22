# WP-505A — Block Inspector

Status: **implemented** (2026-08-22)

## Scope

The Block Inspector projects the bounded `TraceQueryResult` into a stable,
Qt-free `BlockInspectorView`. Each associated block exposes its BFINAL/BTYPE,
logical Deflate input-bit range, inflated output-byte range and physical IDAT
provenance spans. A caller may provide the selected inflated byte and the PNG
stream scanline; the projection marks the owning block and its absolute current
output position without reimplementing Adam7 or Deflate parsing.

The Qt widget is presentation-only. It renders the immutable rows, status,
scanline context and navigation actions (`Show in Hex` and `Show in DEFLATE`).
Navigation emits ranges for the owning application to resolve; the widget does
not read a file or invoke a decoder. The main inspector workspace now exposes
the `DEFLATE / Block` tab while the trace orchestration connection remains the
responsibility of the next integration work package.

## Invariants and verification

- Half-open bit and byte ranges are copied without widening or concatenating
  IDAT payloads.
- Physical span formatting checks offset-plus-length before displaying an end.
- `kReady`, `kPartial`, `kError` and not-yet-available trace states remain
  deterministic and carry the query generation.
- Qt-free projection tests cover selected-block ownership, provenance and
  unavailable/partial states; Qt tests cover rendering and navigation signals.

