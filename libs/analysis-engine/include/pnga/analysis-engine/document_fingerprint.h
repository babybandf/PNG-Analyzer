#ifndef PNGA_ANALYSIS_ENGINE_DOCUMENT_FINGERPRINT_H
#define PNGA_ANALYSIS_ENGINE_DOCUMENT_FINGERPRINT_H

// WP-602D: content-derived document identity for statistics result
// correlation. The fingerprint is computed over the full source in 64 KiB
// windows with FNV-1a 64 and excludes paths, mtime, locale and clock. It is
// not a security hash or a cache-trust anchor.

#include <optional>
#include <string>

#include <pnga/io/byte_source.h>
#include <pnga/statistics/serialization.h>

namespace pnga::analysis_engine {

class CancellationToken;

// Computes the document identity of `source`: its 64-bit size plus the
// fingerprint "fnv1a64-v1:<16 lowercase hex>". The source is read in exactly
// min(65,536, remaining) byte windows in order. Returns nullopt without a
// partial identity when the token is cancelled or a read fails; in that case
// `error` (when non-null) receives a stable message.
std::optional<pnga::statistics::DocumentIdentity> compute_document_identity(
    const pnga::io::IByteSource& source, const CancellationToken* cancellation,
    std::string* error);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_DOCUMENT_FINGERPRINT_H
