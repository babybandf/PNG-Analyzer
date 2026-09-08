#ifndef PNGA_PNG_FORMAT_VIRTUAL_COMPRESSED_STREAM_H
#define PNGA_PNG_FORMAT_VIRTUAL_COMPRESSED_STREAM_H

#include "pnga/io/byte_source.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace pnga::png_format {

struct PhysicalRange {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;

  bool operator==(const PhysicalRange&) const = default;
};

class IVirtualCompressedStream : public pnga::io::IByteSource {
 public:
  ~IVirtualCompressedStream() override = default;

  virtual bool logical_to_physical(std::uint64_t logical_offset,
                                   std::uint64_t length,
                                   std::vector<PhysicalRange>& spans) const
      noexcept = 0;

  virtual std::optional<std::uint64_t> physical_to_logical(
      std::uint64_t physical_offset) const noexcept = 0;
};

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_VIRTUAL_COMPRESSED_STREAM_H
