#ifndef PNGA_ANALYSIS_ENGINE_VALIDATION_H
#define PNGA_ANALYSIS_ENGINE_VALIDATION_H

// WP-600C: one Qt-free validation composition point for CLI and GUI. The
// individual rule modules remain owned by validation; callers do not assemble
// rule order or accidentally omit a category.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/validation/structural.h>

namespace pnga::analysis_engine {

using DocumentValidationReport = pnga::validation::ValidationReport;

DocumentValidationReport validate_document(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_VALIDATION_H
