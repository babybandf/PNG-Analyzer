# PNG Analyzer user guide

PNG Analyzer currently targets one static PNG at a time. It keeps file facts,
decoded stages and trace evidence separate so a malformed or unsupported
region can be reported without discarding structure already verified.

## GUI workflow

1. Launch `build/dev/apps/png-analyzer-gui/pnga_analyzer_gui` (use
   `QT_QPA_PLATFORM=offscreen` only for headless smoke tests).
2. Choose **File → Open…** and select a PNG. Indexing and validation happen
   through the application layer; the widgets do not parse PNG bytes.
3. Use **Chunk List** to select a physical Chunk. The file Hex view can show
   bounded ranges and physical IDAT spans.
4. In Preview, the fixed base stages are **Image**, **Pixels**, **Filter Map**,
   **Filtered** and **Defiltered**. Select a pixel to lock its coordinate;
   hover remains a lightweight local state.
5. Use the Inspector tabs to move from reconstruction and pixel/scanline
   context to source/format context and the bounded DEFLATE Block, Huffman and
   Decode Trace views. Trace replay is on demand and may report partial,
   replaying, cancelled or error state.
6. Read the validation status in the status bar. A report includes a stable
   issue id, severity, first navigable offset where available and a tooltip
   containing the deterministic issue list.

`Esc` clears the locked image coordinate. Direction keys move a locked
coordinate when the viewport or coordinate control has focus. Preview stage
changes preserve the compatible image selection.

## CLI workflow

```text
pnga --version
pnga inspect image.png --json
pnga validate image.png --json
```

`inspect` reports the physical Chunk envelope and remains useful when later
semantic validation fails. `validate` composes structural, integrity,
semantic, resource and zlib preflight rules. JSON field order, issue ids,
messages, offsets and spec references are locale-independent.

## Understanding stages and trace

The [Trace semantics note](development/trace-semantics.md) defines the
relationship between native samples, filtered/reconstructed bytes, inflated
DEFLATE output, tokens and physical file bits. A pixel may map to multiple
filtered bytes, tokens or physical spans; the UI must not collapse that
one-to-many relationship.

## Limits and recovery

- Static PNG is the supported first-delivery format. APNG frame semantics are
  reserved for a later milestone.
- Compare, First Difference and Statistics are not current UI entries.
- Large files are indexed with explicit row/output budgets. A row or trace
  request can be partial or rejected when a configured budget is unsafe.
- A new file increments document generation. Results from an older generation
  are discarded rather than replacing the current selection.
- Validation issues do not imply that all previously indexed Chunk structure is
  lost; inspect the report offset and continue with the verified portion.

For a reproducible failure, preserve the PNG only if it is safe to share; a
minimized synthetic file, CLI JSON and exact build preset are preferred.
