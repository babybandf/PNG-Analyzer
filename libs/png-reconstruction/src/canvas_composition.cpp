#include "pnga/png-reconstruction/canvas_composition.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pnga::png_reconstruction {

namespace {

bool image_size(const RgbaImage& image, std::uint64_t* bytes) noexcept {
  const std::uint64_t pixels =
      static_cast<std::uint64_t>(image.width) * image.height;
  if (image.height != 0 && pixels / image.height != image.width) {
    return false;
  }
  if (pixels > std::numeric_limits<std::uint64_t>::max() / 4) {
    return false;
  }
  *bytes = pixels * 4;
  return *bytes == image.pixels.size();
}

bool rect_inside(const RgbaImage& canvas, FrameRect rect) noexcept {
  return rect.x <= canvas.width && rect.y <= canvas.height &&
         rect.width <= canvas.width - rect.x &&
         rect.height <= canvas.height - rect.y;
}

std::uint64_t rect_bytes(FrameRect rect) noexcept {
  const auto pixels = static_cast<std::uint64_t>(rect.width) * rect.height;
  if (pixels > std::numeric_limits<std::uint64_t>::max() / 4) {
    return 0;
  }
  return pixels * 4;
}

CompositionResult invalid(const char* message) {
  CompositionResult result;
  result.error = message;
  return result;
}

CompositionResult cancelled_result() {
  CompositionResult result;
  result.cancelled = true;
  result.error = "canvas composition cancelled";
  return result;
}

}  // namespace

CompositionResult blend_into(RgbaImage& canvas, const RgbaImage& frame,
                             FrameRect rect, Blend blend,
                             const std::function<bool()>& cancelled) {
  if (cancelled && cancelled()) {
    return cancelled_result();
  }
  std::uint64_t canvas_bytes = 0;
  std::uint64_t frame_bytes = 0;
  if (!image_size(canvas, &canvas_bytes) || !image_size(frame, &frame_bytes) ||
      !rect_inside(canvas, rect) || frame.width != rect.width ||
      frame.height != rect.height) {
    return invalid("canvas, frame or rectangle dimensions are invalid");
  }

  for (std::uint32_t y = 0; y < rect.height; ++y) {
    if (cancelled && cancelled()) {
      return cancelled_result();
    }
    for (std::uint32_t x = 0; x < rect.width; ++x) {
      const std::size_t source_offset =
          static_cast<std::size_t>(y) * rect.width * 4 +
          static_cast<std::size_t>(x) * 4;
      const std::size_t target_offset =
          (static_cast<std::size_t>(rect.y + y) * canvas.width +
           rect.x + x) *
          4;
      if (blend == Blend::kSource) {
        std::copy_n(frame.pixels.begin() + source_offset, 4,
                    canvas.pixels.begin() + target_offset);
        continue;
      }

      const std::uint64_t sa = frame.pixels[source_offset + 3];
      const std::uint64_t da = canvas.pixels[target_offset + 3];
      const std::uint64_t den = sa * 255ull + da * (255ull - sa);
      const std::uint64_t oa = (den + 127ull) / 255ull;
      canvas.pixels[target_offset + 3] = static_cast<std::uint8_t>(oa);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const std::uint64_t sc = frame.pixels[source_offset + channel];
        const std::uint64_t dc = canvas.pixels[target_offset + channel];
        const std::uint64_t numerator =
            sc * sa * 255ull + dc * da * (255ull - sa);
        canvas.pixels[target_offset + channel] = static_cast<std::uint8_t>(
            den == 0 ? 0 : (numerator + den / 2ull) / den);
      }
    }
  }
  return CompositionResult{true, false, {}};
}

CompositionResult dispose_into(RgbaImage& canvas, FrameRect rect,
                               Dispose dispose,
                               std::span<const std::uint8_t> saved_rect,
                               bool first_frame,
                               const std::function<bool()>& cancelled) {
  if (cancelled && cancelled()) {
    return cancelled_result();
  }
  std::uint64_t canvas_bytes = 0;
  if (!image_size(canvas, &canvas_bytes) || !rect_inside(canvas, rect)) {
    return invalid("canvas or rectangle dimensions are invalid");
  }
  if (dispose == Dispose::kNone) {
    return CompositionResult{true, false, {}};
  }
  if (dispose == Dispose::kPrevious &&
      (!first_frame && saved_rect.size() != rect_bytes(rect))) {
    return invalid("previous canvas rectangle has the wrong size");
  }

  for (std::uint32_t y = 0; y < rect.height; ++y) {
    if (cancelled && cancelled()) {
      return cancelled_result();
    }
    for (std::uint32_t x = 0; x < rect.width; ++x) {
      const std::size_t target_offset =
          (static_cast<std::size_t>(rect.y + y) * canvas.width +
           rect.x + x) *
          4;
      if (dispose == Dispose::kBackground || first_frame) {
        std::fill_n(canvas.pixels.begin() + target_offset, 4,
                    std::uint8_t{0});
      } else {
        const std::size_t saved_offset =
            static_cast<std::size_t>(y) * rect.width * 4 +
            static_cast<std::size_t>(x) * 4;
        std::copy_n(saved_rect.begin() + saved_offset, 4,
                    canvas.pixels.begin() + target_offset);
      }
    }
  }
  return CompositionResult{true, false, {}};
}

}  // namespace pnga::png_reconstruction
