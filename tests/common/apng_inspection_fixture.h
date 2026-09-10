// WP-APNG-INSPECT shared inspection fixtures (T02). The helpers build a
// deterministic two-frame APNG request and a dual-wrapped payload fixture
// where one compressed payload exists both as a static PNG IDAT and as an
// APNG frame stream. Test buffers stay within 64 KiB; production code must
// never concatenate full frame payloads.

#ifndef PNGA_TESTS_COMMON_APNG_INSPECTION_FIXTURE_H
#define PNGA_TESTS_COMMON_APNG_INSPECTION_FIXTURE_H

#include "apng_fixture.h"
#include "test_png_helpers.h"

#include <pnga/analysis-engine/frame_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-reconstruction/rgba_delivery.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pnga_test {

// Fixed two-frame inspection request: canvas 16x32 RGBA8, two 2x3 frames at
// offset (10,20). Frame 0 uses APNG_BLEND_OP_SOURCE, frame 1 uses
// APNG_BLEND_OP_OVER. Generation 7 and request serial 11 are frozen so
// tickets in tests are deterministic.
inline pnga::analysis_engine::FrameRequest inspection_request(
    std::uint32_t ordinal = 0) {
  using namespace pnga;
  const std::array<png_format::FrameControl, 2> frames{
      png_format::FrameControl{0, 2, 3, 10, 20, 1, 100, 0, 0},
      png_format::FrameControl{0, 2, 3, 10, 20, 1, 100, 0, 1}};
  auto source = std::make_shared<const io::MemoryByteSource>(
      make_apng_format(/*default_is_frame=*/false, frames, ApngFormat{}, {},
                       {}, {}, /*canvas_width=*/16, /*canvas_height=*/32));
  pnga::analysis_engine::FrameRequest request;
  request.generation = 7;
  request.request_serial = 11;
  request.ordinal = ordinal;
  request.source = source;
  request.index = std::make_shared<const png_format::AnimationIndex>(
      png_format::index_animation(*source, png_format::AnimationLimits{},
                                  [] { return false; }));
  request.canvas_header = pnga::png_reconstruction::ImageHeader{
      16, 32, 8, 6, false};
  return request;
}

// One compressed 2x3 payload wrapped twice: once as a standalone static PNG
// (IDAT) and once as an APNG (fdAT frame 0 at offset (10,20) on a 16x32
// canvas). Logical stream bytes are identical; physical file offsets are
// not. Palette/tRNS test values are copied into both wrappers when given.
struct DualWrappedPayload {
  std::vector<std::byte> static_png;
  std::vector<std::byte> apng;
  std::vector<std::byte> frame_payload;
  pnga::png_reconstruction::ImageHeader header{2, 3, 8, 6, false};
  pnga::png_reconstruction::DeliveryContext delivery;
};

inline DualWrappedPayload make_dual_wrapped_payload(
    const ApngFormat& format = ApngFormat{},
    std::span<const std::byte> palette = {},
    std::span<const std::byte> transparency = {}) {
  DualWrappedPayload dual;
  dual.header = pnga::png_reconstruction::ImageHeader{
      2, 3, format.bit_depth, format.color_type, format.interlace};
  const auto pattern = [&format](std::uint32_t x, std::uint32_t y,
                                 std::uint8_t channel) {
    return apng_pattern_sample(x, y, channel, format.bit_depth);
  };
  dual.frame_payload = encode_apng_frame_payload(2, 3, format, pattern);

  const auto ihdr = [](std::uint32_t width, std::uint32_t height,
                       const ApngFormat& fmt) {
    std::vector<std::byte> data;
    append_u32(data, width);
    append_u32(data, height);
    data.insert(data.end(),
                {apng_byte(fmt.bit_depth), apng_byte(fmt.color_type),
                 apng_byte(0), apng_byte(0),
                 apng_byte(fmt.interlace ? 1u : 0u)});
    return data;
  };

  auto& static_png = dual.static_png;
  static_png.insert(static_png.end(),
                    pnga::png_format::kPngSignature.begin(),
                    pnga::png_format::kPngSignature.end());
  append_apng_chunk(static_png, "IHDR", ihdr(2, 3, format));
  if (!palette.empty()) {
    append_apng_chunk(static_png, "PLTE", palette);
  }
  if (!transparency.empty()) {
    append_apng_chunk(static_png, "tRNS", transparency);
  }
  append_apng_chunk(static_png, "IDAT", dual.frame_payload);
  append_apng_chunk(static_png, "IEND", {});

  auto& apng = dual.apng;
  apng.insert(apng.end(), pnga::png_format::kPngSignature.begin(),
              pnga::png_format::kPngSignature.end());
  append_apng_chunk(apng, "IHDR", ihdr(16, 32, format));
  if (!palette.empty()) {
    append_apng_chunk(apng, "PLTE", palette);
  }
  if (!transparency.empty()) {
    append_apng_chunk(apng, "tRNS", transparency);
  }
  std::vector<std::byte> actl;
  append_u32(actl, 1);
  append_u32(actl, 0);
  append_apng_chunk(apng, "acTL", actl);
  std::vector<std::byte> fctl;
  append_u32(fctl, 0);
  append_u32(fctl, 2);
  append_u32(fctl, 3);
  append_u32(fctl, 10);
  append_u32(fctl, 20);
  append_u16(fctl, 1);
  append_u16(fctl, 100);
  apng.push_back(apng_byte(0));
  apng.push_back(apng_byte(0));
  append_apng_chunk(apng, "fcTL", fctl);
  std::vector<std::byte> fdat;
  append_u32(fdat, 1);  // fdAT sequence follows the fcTL sequence
  fdat.insert(fdat.end(), dual.frame_payload.begin(),
              dual.frame_payload.end());
  append_apng_chunk(apng, "fdAT", fdat);
  append_apng_chunk(apng, "IEND", {});

  if (static_png.size() + apng.size() > 64u * 1024u) {
    return DualWrappedPayload{};
  }

  pnga::io::MemoryByteSource apng_source(dual.apng);
  const auto index = pnga::png_format::index_animation(
      apng_source, pnga::png_format::AnimationLimits{}, [] { return false; });
  dual.delivery =
      pnga::analysis_engine::delivery_context_from(index, dual.header);
  return dual;
}

}  // namespace pnga_test

#endif  // PNGA_TESTS_COMMON_APNG_INSPECTION_FIXTURE_H
