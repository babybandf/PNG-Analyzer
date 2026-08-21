#ifndef PNGA_CLI_REPORT_H
#define PNGA_CLI_REPORT_H

// WP-103: deterministic JSON reports for `pnga inspect` and `pnga validate`.
// Field order and key names are fixed; output must not depend on locale, clock
// or iteration order.

#include <pnga/png-format/chunk_index.h>

#include <string>

namespace pnga::cli {

// Stable machine-readable name for an issue kind (used in JSON and tests).
const char* issue_kind_text(pnga::png_format::ChunkIssueKind kind);

// `pnga inspect <file> --json`: file identity, signature status, the physical
// Chunk tree and any issues found while indexing.
std::string inspect_json(const std::string& file,
                         const pnga::png_format::ChunkIndex& index);

// `pnga validate <file> --json`: file identity, validity and the issue list.
std::string validate_json(const std::string& file,
                          const pnga::png_format::ChunkIndex& index);

// `pnga inspect/validate <file>` read-failure report (file could not be read).
std::string error_json(const std::string& file, int error_code);

}  // namespace pnga::cli

#endif  // PNGA_CLI_REPORT_H
