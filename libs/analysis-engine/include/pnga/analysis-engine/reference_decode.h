#ifndef PNGA_ANALYSIS_ENGINE_REFERENCE_DECODE_H
#define PNGA_ANALYSIS_ENGINE_REFERENCE_DECODE_H

// WP-204: GUI-facing reference decode. The GUI and CLI consume the libpng
// Reference Backend through the analysis engine (layout §7: png-analyzer-gui
// depends on pnga_analysis_engine, never on backend-libpng directly).

#include <pnga/backend-libpng/reference.h>
#include <pnga/io/byte_source.h>

namespace pnga::analysis_engine {

// Delegates to the libpng Reference Backend. Synchronous; callers must run it
// off the UI thread.
pnga::backend_libpng::ReferenceResult decode_reference(
    const pnga::io::IByteSource& source);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_REFERENCE_DECODE_H
