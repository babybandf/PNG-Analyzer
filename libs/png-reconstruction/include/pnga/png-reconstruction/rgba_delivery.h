#ifndef PNGA_PNG_RECONSTRUCTION_RGBA_DELIVERY_H
#define PNGA_PNG_RECONSTRUCTION_RGBA_DELIVERY_H

#include "pnga/png-reconstruction/native_samples.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pnga::png_reconstruction {

struct RgbaImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

struct DeliveryContext {
  std::vector<std::array<std::uint8_t, 3>> palette;
  std::vector<std::uint8_t> palette_alpha;
  std::optional<std::uint16_t> transparent_gray;
  std::optional<std::array<std::uint16_t, 3>> transparent_rgb;
};

struct DeliveryResult {
  bool success = false;
  bool cancelled = false;
  std::string error;
  RgbaImage image;
};

DeliveryResult deliver_rgba8(
    const NativeImage& native, const DeliveryContext& context,
    std::uint64_t max_bytes, const std::function<bool()>& cancelled);

}  // namespace pnga::png_reconstruction

#endif  // PNGA_PNG_RECONSTRUCTION_RGBA_DELIVERY_H
