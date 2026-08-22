#ifndef PNGA_PNG_FORMAT_CHECKSUM_H
#define PNGA_PNG_FORMAT_CHECKSUM_H

// WP-600A: bounded PNG checksum helpers. CRC is calculated over a Chunk type
// followed by its data; the helper never materializes the chunk payload.

#include "pnga/io/byte_source.h"
#include "pnga/png-format/chunk_index.h"

#include <cstdint>
#include <optional>

namespace pnga::png_format {

// Returns the CRC calculated over the four type bytes and the indexed data
// span. A null result means that the source no longer contains the indexed
// range or a bounded read failed.
std::optional<std::uint32_t> calculate_chunk_crc(
    const pnga::io::IByteSource& source, const ChunkNode& node) noexcept;

// Reads the stored big-endian CRC from the indexed CRC span. A null result
// means that the source no longer contains the four-byte span.
std::optional<std::uint32_t> read_chunk_crc(
    const pnga::io::IByteSource& source, const ChunkNode& node) noexcept;

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_CHECKSUM_H
