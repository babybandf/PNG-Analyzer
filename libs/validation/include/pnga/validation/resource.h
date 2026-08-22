#ifndef PNGA_VALIDATION_RESOURCE_H
#define PNGA_VALIDATION_RESOURCE_H

// WP-600B: checked resource budgets for image dimensions and scanline bytes.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include "pnga/validation/structural.h"

namespace pnga::validation {

inline constexpr std::uint64_t kMaxImageDimension = 1ULL << 28;
inline constexpr std::uint64_t kMaxDecodedScanlineBytes = 1ULL << 34;

ValidationReport validate_resources(const pnga::io::IByteSource& source,
                                    const pnga::png_format::ChunkIndex& index);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_RESOURCE_H
