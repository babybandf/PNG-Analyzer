#ifndef PNGA_PNG_FORMAT_VIRTUAL_FRAME_STREAM_H
#define PNGA_PNG_FORMAT_VIRTUAL_FRAME_STREAM_H

#include "pnga/png-format/animation_index.h"
#include "pnga/png-format/chunk_index.h"
#include "pnga/png-format/virtual_compressed_stream.h"

#include <cstdint>
#include <memory>

namespace pnga::png_format {

std::shared_ptr<const IVirtualCompressedStream> make_frame_stream(
    std::shared_ptr<const pnga::io::IByteSource> source,
    std::shared_ptr<const AnimationIndex> index, std::uint32_t ordinal);

std::shared_ptr<const IVirtualCompressedStream> make_idat_stream(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const ChunkIndex& index);

}  // namespace pnga::png_format

#endif  // PNGA_PNG_FORMAT_VIRTUAL_FRAME_STREAM_H
