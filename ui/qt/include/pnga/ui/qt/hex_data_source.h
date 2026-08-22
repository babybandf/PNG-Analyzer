#ifndef PNGA_UI_QT_HEX_DATA_SOURCE_H
#define PNGA_UI_QT_HEX_DATA_SOURCE_H

// WP-5U4A: lifetime-safe, windowed byte sources for HexView. Implementations
// copy only the caller's requested window and never concatenate IDAT payloads.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pnga::ui::qt {

class HexDataSource {
 public:
  virtual ~HexDataSource() = default;
  virtual const char* name() const noexcept = 0;
  virtual std::uint64_t size() const noexcept = 0;
  virtual bool read(std::uint64_t offset, std::byte* out,
                    std::size_t length) const noexcept = 0;
};

std::shared_ptr<const HexDataSource> make_file_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source);

std::shared_ptr<const HexDataSource> make_idat_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const pnga::png_format::VirtualIDATStream& stream);

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HEX_DATA_SOURCE_H
