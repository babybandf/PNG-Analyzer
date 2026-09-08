#include "pnga/png-reconstruction/rgba_delivery.h"

#include <cstddef>
#include <limits>

namespace pnga::png_reconstruction {

namespace {

bool valid_combo(std::uint8_t bit_depth, std::uint8_t color_type) noexcept {
  switch (color_type) {
    case 0:
      return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
             bit_depth == 8 || bit_depth == 16;
    case 2:
      return bit_depth == 8 || bit_depth == 16;
    case 3:
      return bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
             bit_depth == 8;
    case 4:
    case 6:
      return bit_depth == 8 || bit_depth == 16;
    default:
      return false;
  }
}

std::optional<std::uint64_t> checked_mul(std::uint64_t a,
                                         std::uint64_t b) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
    return std::nullopt;
  }
  return a * b;
}

std::uint8_t to_u8(std::uint16_t sample, std::uint8_t bit_depth) noexcept {
  if (bit_depth == 16) {
    return static_cast<std::uint8_t>(sample >> 8);
  }
  const std::uint32_t max_sample = (1u << bit_depth) - 1u;
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(sample) * 255u) / max_sample);
}

DeliveryResult fail(const char* message) {
  DeliveryResult result;
  result.error = message;
  return result;
}

}  // namespace

DeliveryResult deliver_rgba8(
    const NativeImage& native, const DeliveryContext& context,
    std::uint64_t max_bytes, const std::function<bool()>& cancelled) {
  DeliveryResult result;
  auto cancel = [&result]() {
    result.success = false;
    result.cancelled = true;
    result.error = "RGBA delivery cancelled";
    result.image = RgbaImage{};
    return result;
  };
  if (cancelled && cancelled()) {
    return cancel();
  }
  if (native.width == 0 || native.height == 0 ||
      !valid_combo(native.bit_depth, native.color_type)) {
    return fail("invalid native image header");
  }
  const std::uint8_t channels = channels_for_color_type(native.color_type);
  const auto area = checked_mul(native.width, native.height);
  const auto sample_count = area.has_value()
                                ? checked_mul(*area, channels)
                                : std::nullopt;
  const auto pixel_bytes = area.has_value() ? checked_mul(*area, 4) : std::nullopt;
  if (!sample_count.has_value() || !pixel_bytes.has_value() ||
      native.samples.size() != static_cast<std::size_t>(*sample_count)) {
    return fail("native sample count does not match the image header");
  }
  if (*pixel_bytes > max_bytes ||
      *pixel_bytes > std::numeric_limits<std::size_t>::max()) {
    return fail("RGBA delivery exceeds the size limit");
  }
  if (context.palette.size() > 256 || context.palette_alpha.size() > 256) {
    return fail("palette exceeds the size limit");
  }
  if (native.color_type == 3 && context.palette.empty()) {
    return fail("palette is missing");
  }

  result.image.width = native.width;
  result.image.height = native.height;
  result.image.pixels.resize(static_cast<std::size_t>(*pixel_bytes));

  for (std::uint32_t y = 0; y < native.height; ++y) {
    if (cancelled && cancelled()) {
      return cancel();
    }
    for (std::uint32_t x = 0; x < native.width; ++x) {
      const std::uint64_t pixel =
          static_cast<std::uint64_t>(y) * native.width + x;
      const std::size_t sample_offset =
          static_cast<std::size_t>(pixel * channels);
      const std::size_t output_offset = static_cast<std::size_t>(pixel * 4);
      auto& out = result.image.pixels;

      switch (native.color_type) {
        case 0: {
          const auto gray = native.samples[sample_offset];
          const bool transparent = context.transparent_gray.has_value() &&
                                   gray == *context.transparent_gray;
          const auto value = to_u8(gray, native.bit_depth);
          out[output_offset] = value;
          out[output_offset + 1] = value;
          out[output_offset + 2] = value;
          out[output_offset + 3] = transparent ? 0 : 255;
          break;
        }
        case 2: {
          const auto r = native.samples[sample_offset];
          const auto g = native.samples[sample_offset + 1];
          const auto b = native.samples[sample_offset + 2];
          const bool transparent = context.transparent_rgb.has_value() &&
                                   std::array<std::uint16_t, 3>{r, g, b} ==
                                       *context.transparent_rgb;
          out[output_offset] = to_u8(r, native.bit_depth);
          out[output_offset + 1] = to_u8(g, native.bit_depth);
          out[output_offset + 2] = to_u8(b, native.bit_depth);
          out[output_offset + 3] = transparent ? 0 : 255;
          break;
        }
        case 3: {
          const auto palette_index = native.samples[sample_offset];
          if (palette_index >= context.palette.size()) {
            return fail("palette index is out of range");
          }
          const auto color = context.palette[palette_index];
          out[output_offset] = color[0];
          out[output_offset + 1] = color[1];
          out[output_offset + 2] = color[2];
          out[output_offset + 3] = palette_index < context.palette_alpha.size()
                                       ? context.palette_alpha[palette_index]
                                       : 255;
          break;
        }
        case 4: {
          const auto value = to_u8(native.samples[sample_offset],
                                   native.bit_depth);
          out[output_offset] = value;
          out[output_offset + 1] = value;
          out[output_offset + 2] = value;
          out[output_offset + 3] =
              to_u8(native.samples[sample_offset + 1], native.bit_depth);
          break;
        }
        case 6:
          out[output_offset] =
              to_u8(native.samples[sample_offset], native.bit_depth);
          out[output_offset + 1] =
              to_u8(native.samples[sample_offset + 1], native.bit_depth);
          out[output_offset + 2] =
              to_u8(native.samples[sample_offset + 2], native.bit_depth);
          out[output_offset + 3] =
              to_u8(native.samples[sample_offset + 3], native.bit_depth);
          break;
        default:
          return fail("invalid native image color type");
      }
    }
  }

  if (cancelled && cancelled()) {
    return cancel();
  }
  result.success = true;
  return result;
}

}  // namespace pnga::png_reconstruction
