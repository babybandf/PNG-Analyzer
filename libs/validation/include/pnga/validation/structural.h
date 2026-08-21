#ifndef PNGA_VALIDATION_STRUCTURAL_H
#define PNGA_VALIDATION_STRUCTURAL_H

// WP-102: first batch of structural PNG validation rules. The validator is a
// pure analysis pass over a physical Chunk index: it never throws, never
// parses chunk data and never terminates indexing. Rule ids are stable and
// reports are deterministic.

#include <pnga/png-format/chunk_index.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::validation {

enum class Severity { kInfo, kWarning, kError };

struct ValidationIssue {
  std::string rule_id;   // stable machine id, e.g. "idat_not_contiguous"
  Severity severity;
  std::string message;   // stable human text
  std::uint64_t offset;  // byte offset where the problem applies
  std::string spec_ref;  // stable spec reference, e.g. "PNG:5.2"
};

struct ValidationReport {
  std::vector<ValidationIssue> issues;

  bool valid() const noexcept { return issues.empty(); }
};

// Runs the first structural rule batch over `index`:
//   signature_invalid / signature_truncated / chunk_truncated (from the
//   Chunk Index issues), ihdr_required / ihdr_duplicate, iend_required,
//   idat_not_contiguous, data_after_iend.
// Deterministic: identical input yields an identical report.
ValidationReport validate_structure(const pnga::png_format::ChunkIndex& index);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_STRUCTURAL_H
