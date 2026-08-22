// WP-600C: deterministic validation composition for application consumers.

#include "pnga/analysis-engine/validation.h"

#include <pnga/validation/decode.h>
#include <pnga/validation/integrity.h>
#include <pnga/validation/resource.h>
#include <pnga/validation/semantic.h>

#include <iterator>

namespace pnga::analysis_engine {

DocumentValidationReport validate_document(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index) {
  DocumentValidationReport result = pnga::validation::validate_structure(index);
  auto append = [&result](pnga::validation::ValidationReport report) {
    result.issues.insert(result.issues.end(),
                         std::make_move_iterator(report.issues.begin()),
                         std::make_move_iterator(report.issues.end()));
  };
  append(pnga::validation::validate_integrity(source, index));
  append(pnga::validation::validate_semantics(source, index));
  append(pnga::validation::validate_resources(source, index));
  append(pnga::validation::validate_decode_preflight(source, index));
  return result;
}

}  // namespace pnga::analysis_engine
