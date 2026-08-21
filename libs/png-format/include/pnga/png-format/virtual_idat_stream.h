#ifndef PNGA_PNG_FORMAT_VIRTUAL_IDAT_STREAM_H
#define PNGA_PNG_FORMAT_VIRTUAL_IDAT_STREAM_H

// WP-201: VirtualIDATStream (ADR-0005). Multiple IDAT payloads are exposed as
// one logical byte stream without concatenating them into a new buffer.
// Building the stream allocates only a segment table; reads copy into a caller
// buffer and logical->physical mapping returns physical offsets.

#include "pnga/io/byte_source.h"
#include "pnga/png-format/chunk_index.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace pnga::png_format {

// One physical IDAT data segment (header and CRC excluded).
struct IdatSegment {
  std::uint64_t physical_offset = 0;  // first data byte in the file
  std::uint64_t length = 0;           // data bytes
  std::uint64_t logical_start = 0;    // position in the virtual stream
};

// A physical byte range (offset/length pair, no views into any source).
struct PhysicalRange {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;

  bool operator==(const PhysicalRange&) const = default;
};

// Logical-to-physical mapping for the concatenation of all IDAT data payloads
// in a Chunk index. The ChunkIndex is borrowed and must outlive the stream.
class VirtualIDATStream {
 public:
  explicit VirtualIDATStream(const ChunkIndex& index);

  // Total logical bytes (sum of all IDAT data lengths).
  std::uint64_t size() const noexcept { return total_; }
  std::size_t segment_count() const noexcept { return segments_.size(); }
  const IdatSegment& segment(std::size_t i) const noexcept {
    return segments_[i];
  }

  // Copies `length` logical bytes at `logical_offset` into `out` (caller-owned,
  // `length` bytes), reading across segment boundaries without concatenation.
  // Returns false without touching `out` when the range is out of bounds.
  bool read(const pnga::io::IByteSource& source, std::uint64_t logical_offset,
            std::byte* out, std::size_t length) const noexcept;

  // Splits the logical range [logical_offset, logical_offset + length) into
  // the physical ranges that cover it, in order. Returns false when the range
  // is out of bounds (nothing is appended).
  bool logical_to_physical(std::uint64_t logical_offset, std::uint64_t length,
                           std::vector<PhysicalRange>& spans) const noexcept;

  // Maps a physical byte offset to its logical position when it lies inside an
  // IDAT data region; returns std::nullopt for headers, CRCs and non-IDAT data.
  std::optional<std::uint64_t> physical_to_logical(
      std::uint64_t physical_offset) const noexcept;

 private:
  std::vector<IdatSegment> segments_;
  std::uint64_t total_ = 0;
};

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_VIRTUAL_IDAT_STREAM_H
