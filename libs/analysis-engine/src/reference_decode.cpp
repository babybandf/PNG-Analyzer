// WP-204 analysis-engine reference decode pass-through.

#include "pnga/analysis-engine/reference_decode.h"

namespace pnga::analysis_engine {

pnga::backend_libpng::ReferenceResult decode_reference(
    const pnga::io::IByteSource& source) {
  return pnga::backend_libpng::decode_reference(source);
}

}  // namespace pnga::analysis_engine
