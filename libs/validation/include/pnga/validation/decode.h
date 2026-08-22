#ifndef PNGA_VALIDATION_DECODE_H
#define PNGA_VALIDATION_DECODE_H

// WP-600B: bounded zlib/IDAT decode preflight. This validates the wrapper
// contract only; Inflate remains owned by deflate-runtime/analysis-engine.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include "pnga/validation/structural.h"

namespace pnga::validation {

ValidationReport validate_decode_preflight(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_DECODE_H
