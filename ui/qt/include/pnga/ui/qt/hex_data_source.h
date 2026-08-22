#ifndef PNGA_UI_QT_HEX_DATA_SOURCE_H
#define PNGA_UI_QT_HEX_DATA_SOURCE_H

// WP-5U4A: lifetime-safe, windowed byte sources for HexView. Implementations
// copy only the caller's requested window and never concatenate IDAT payloads.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/analysis-engine/stage_analysis.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pnga::ui::qt {

enum class HexDataStatus { kReady, kUnavailable, kReplaying, kError };

const char* hex_data_status_text(HexDataStatus status) noexcept;

class HexDataSource {
 public:
  virtual ~HexDataSource() = default;
  virtual const char* name() const noexcept = 0;
  virtual HexDataStatus status() const noexcept = 0;
  virtual std::uint64_t size() const noexcept = 0;
  virtual bool read(std::uint64_t offset, std::byte* out,
                    std::size_t length) const noexcept = 0;
};

std::shared_ptr<const HexDataSource> make_file_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source);

std::shared_ptr<const HexDataSource> make_idat_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const pnga::png_format::VirtualIDATStream& stream);

std::shared_ptr<const HexDataSource> make_inflated_hex_source(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages);

std::shared_ptr<const HexDataSource> make_defiltered_hex_source(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages);

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HEX_DATA_SOURCE_H
