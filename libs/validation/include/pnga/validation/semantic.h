#ifndef PNGA_VALIDATION_SEMANTIC_H
#define PNGA_VALIDATION_SEMANTIC_H

// WP-600B: PNG IHDR semantic rules. Parsing is bounded to the fixed 13-byte
// IHDR body and remains independent of decoders and Qt.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include "pnga/validation/structural.h"

#include <cstdint>
#include <optional>

namespace pnga::validation {

struct PngHeaderFields {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;
  std::uint8_t color_type = 0;
  std::uint8_t compression_method = 0;
  std::uint8_t filter_method = 0;
  std::uint8_t interlace_method = 0;
  std::uint64_t header_offset = 0;
};

// Reads the first IHDR only when its body is exactly 13 readable bytes.
std::optional<PngHeaderFields> read_ihdr(
    const pnga::io::IByteSource& source,
    const pnga::png_format::ChunkIndex& index) noexcept;

ValidationReport validate_semantics(const pnga::io::IByteSource& source,
                                    const pnga::png_format::ChunkIndex& index);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_SEMANTIC_H
