# pnga_backend_libpng

libpng Reference Backend (REPOSITORY_LAYOUT.md §5.9, ADR-0002, ADR-0008).

## Responsibility

- Public libpng read API integration with a custom read callback.
- Reference metadata, rows and final RGBA8 pixels.
- Structured warning capture and error/longjmp containment.
- Version reporting for differential evidence.

## Non-goals

- Token-level Deflate traces or reverse-filter intermediates (Trace Backend).
- Access to libpng private structures or copied internal functions.

## Public targets

- `pnga_backend_libpng` (alias `pnga::backend_libpng`). The only production
  target allowed to link `PNG::PNG` (layout §8.2).

## Allowed dependencies

- `pnga_core`, `pnga_io`, `pnga_png_format`, `pnga_trace_model`, libpng, zlib.
  Never Qt (ADR-0003).
