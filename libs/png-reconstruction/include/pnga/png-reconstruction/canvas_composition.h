#ifndef PNGA_PNG_RECONSTRUCTION_CANVAS_COMPOSITION_H
#define PNGA_PNG_RECONSTRUCTION_CANVAS_COMPOSITION_H

#include "pnga/png-reconstruction/rgba_delivery.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace pnga::png_reconstruction {

struct FrameRect {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

enum class Blend { kSource, kOver };
enum class Dispose { kNone, kBackground, kPrevious };

struct CompositionResult {
  bool success = false;
  bool cancelled = false;
  std::string error;
};

CompositionResult blend_into(RgbaImage& canvas, const RgbaImage& frame,
                             FrameRect rect, Blend blend,
                             const std::function<bool()>& cancelled);

CompositionResult dispose_into(RgbaImage& canvas, FrameRect rect,
                               Dispose dispose,
                               std::span<const std::uint8_t> saved_rect,
                               bool first_frame,
                               const std::function<bool()>& cancelled);

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_CANVAS_COMPOSITION_H
