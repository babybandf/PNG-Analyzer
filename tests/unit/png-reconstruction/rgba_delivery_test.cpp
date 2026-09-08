#include <pnga/png-reconstruction/rgba_delivery.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using pnga::png_reconstruction::DeliveryContext;
using pnga::png_reconstruction::NativeImage;
using pnga::png_reconstruction::RgbaImage;
using pnga::png_reconstruction::deliver_rgba8;

TEST_CASE("RGBA delivery shrinks native 16-bit channels deterministically",
          "[png-reconstruction][apng][wp702]") {
  NativeImage native;
  native.width = 1;
  native.height = 1;
  native.bit_depth = 16;
  native.color_type = 6;
  native.channels = 4;
  native.samples = {0x1234, 0xabcd, 0xffff, 0x8001};

  const auto result = deliver_rgba8(native, DeliveryContext{}, 4,
                                     [] { return false; });
  REQUIRE(result.success);
  REQUIRE(result.image.width == 1);
  REQUIRE(result.image.height == 1);
  REQUIRE(result.image.pixels == std::vector<std::uint8_t>{0x12, 0xab, 0xff,
                                                            0x80});
}

TEST_CASE("Gray, RGB transparency and palette alpha become RGBA",
          "[png-reconstruction][apng][wp702]") {
  NativeImage gray{2, 1, 8, 0, 1, {0, 255}};
  DeliveryContext gray_context;
  gray_context.transparent_gray = 255;
  const auto gray_result =
      deliver_rgba8(gray, gray_context, 8, [] { return false; });
  REQUIRE(gray_result.success);
  REQUIRE(gray_result.image.pixels ==
          std::vector<std::uint8_t>{0, 0, 0, 255, 255, 255, 255, 0});

  NativeImage palette{2, 1, 8, 3, 1, {0, 1}};
  DeliveryContext palette_context;
  palette_context.palette = {{255, 0, 0}, {0, 255, 0}};
  palette_context.palette_alpha = {128};
  const auto palette_result =
      deliver_rgba8(palette, palette_context, 8, [] { return false; });
  REQUIRE(palette_result.success);
  REQUIRE(palette_result.image.pixels ==
          std::vector<std::uint8_t>{255, 0, 0, 128, 0, 255, 0, 255});

  NativeImage rgb{1, 1, 8, 2, 3, {1, 2, 3}};
  DeliveryContext rgb_context;
  rgb_context.transparent_rgb = std::array<std::uint16_t, 3>{1, 2, 3};
  const auto rgb_result =
      deliver_rgba8(rgb, rgb_context, 4, [] { return false; });
  REQUIRE(rgb_result.success);
  REQUIRE(rgb_result.image.pixels ==
          std::vector<std::uint8_t>{1, 2, 3, 0});
}

TEST_CASE("RGBA delivery rejects unsafe work and invalid palette indices",
          "[png-reconstruction][apng][wp702]") {
  NativeImage native{2, 1, 8, 6, 4,
                     {0, 0, 0, 255, 255, 255, 255, 255}};
  REQUIRE_FALSE(deliver_rgba8(native, DeliveryContext{}, 7,
                               [] { return false; })
                    .success);

  NativeImage palette{1, 1, 8, 3, 1, {2}};
  DeliveryContext context;
  context.palette = {{0, 0, 0}, {255, 255, 255}};
  const auto invalid = deliver_rgba8(palette, context, 4,
                                     [] { return false; });
  REQUIRE_FALSE(invalid.success);
  REQUIRE_FALSE(invalid.error.empty());
}

TEST_CASE("RGBA delivery observes cancellation between rows",
          "[png-reconstruction][apng][wp702]") {
  NativeImage native{1, 4, 8, 6, 4,
                     {0, 0, 0, 255, 0, 0, 0, 255,
                      0, 0, 0, 255, 0, 0, 0, 255}};
  int checks = 0;
  const auto result = deliver_rgba8(native, DeliveryContext{}, 16,
                                     [&checks] { return ++checks > 2; });
  REQUIRE_FALSE(result.success);
  REQUIRE(result.cancelled);
  REQUIRE(result.image.pixels.empty());
}
