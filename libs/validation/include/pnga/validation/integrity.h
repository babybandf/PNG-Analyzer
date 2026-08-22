#ifndef PNGA_VALIDATION_INTEGRITY_H
#define PNGA_VALIDATION_INTEGRITY_H

// WP-600A: deterministic, Qt-free integrity rules. Chunk CRCs are checked
// without copying payloads; Adler verification accepts the decoder-owned
// inflated byte span so validation does not depend on a decoder or allocate a
// second logical IDAT stream.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include "pnga/validation/structural.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace pnga::validation {

// Checks every indexed Chunk CRC and the logical IDAT envelope. When
// `inflated_idat` is supplied by a decoder, also checks the stored Adler-32
// trailer against that borrowed, immutable output span.
ValidationReport validate_integrity(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index,
    std::optional<std::span<const std::byte>> inflated_idat = std::nullopt);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_INTEGRITY_H
