#ifndef PNGA_PNG_FORMAT_CHUNK_DETAIL_H
#define PNGA_PNG_FORMAT_CHUNK_DETAIL_H

// WP-5U8: bounded, Qt-free presentation data for a selected PNG Chunk.
// ChunkDetail never owns the source bytes and deliberately treats IDAT as an
// opaque compressed stream.

#include "pnga/png-format/chunk_index.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::png_format {

struct ChunkDetailField {
  std::string name;
  std::string value;
};

struct ChunkDetail {
  std::string type;
  std::uint64_t data_length = 0;
  std::uint64_t data_offset = 0;
  std::uint64_t crc_offset = 0;
  bool basic_only = true;
  std::vector<ChunkDetailField> fields;
};

// Decodes bounded fields for the selected Chunk. The returned strings are
// deterministic and locale-independent. Invalid or unsupported payloads are
// represented by an explanatory row and never cause an unbounded allocation.
ChunkDetail describe_chunk(const pnga::io::IByteSource& source,
                           const ChunkNode& node);

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_CHUNK_DETAIL_H
