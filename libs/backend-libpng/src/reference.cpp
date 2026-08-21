// WP-202 libpng Reference Backend. Public libpng read API only (ADR-0008);
// all longjmp/error handling stays inside decode_reference().

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pnga/backend-libpng/reference.h"

#include <png.h>

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pnga::backend_libpng {

namespace {

// Cursor over the borrowed ByteSource used by the libpng read callback.
struct ReadContext {
  const pnga::io::IByteSource* source = nullptr;
  std::uint64_t pos = 0;
};

// Captured libpng errors and warnings; shared via the error pointer.
struct ErrorState {
  std::string error;
  std::vector<std::string> warnings;
};

void read_callback(png_structp png, png_bytep out, png_size_t length) {
  auto* ctx = static_cast<ReadContext*>(png_get_io_ptr(png));
  std::byte* dst = reinterpret_cast<std::byte*>(out);
  const std::uint64_t size = ctx->source->size();
  const std::uint64_t remaining = size > ctx->pos ? size - ctx->pos : 0;
  const std::size_t take =
      static_cast<std::size_t>(std::min<std::uint64_t>(length, remaining));
  if (take != 0) {
    ctx->source->read(ctx->pos, dst, take);
    ctx->pos += take;
  }
  // Zero-fill the remainder so libpng sees clean EOF instead of garbage.
  std::memset(out + take, 0, length - take);
}

void error_callback(png_structp png, png_const_charp message) {
  auto* state = static_cast<ErrorState*>(png_get_error_ptr(png));
  if (message != nullptr) {
    state->error = message;
  }
  longjmp(png_jmpbuf(png), 1);
}

void warning_callback(png_structp png, png_const_charp message) {
  auto* state = static_cast<ErrorState*>(png_get_error_ptr(png));
  if (message != nullptr) {
    state->warnings.emplace_back(message);
  }
}

}  // namespace

const char* libpng_version() { return PNG_LIBPNG_VER_STRING; }

ReferenceResult decode_reference(const pnga::io::IByteSource& source) {
  ReferenceResult result;
  ErrorState state;

  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, &state, error_callback,
                             warning_callback);
  if (png == nullptr) {
    result.error = "libpng: png_create_read_struct failed";
    return result;
  }
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    result.error = "libpng: png_create_info_struct failed";
    return result;
  }

  if (setjmp(png_jmpbuf(png)) != 0) {
    // Longjmp target for every libpng error (error_callback).
    png_destroy_read_struct(&png, &info, nullptr);
    result.success = false;
    result.error = state.error.empty() ? "libpng: decode error" : state.error;
    return result;
  }

  // Hostile-input bounds: cap allocations and chunk caches (AGENTS.md treats
  // input as untrusted).
  png_set_user_limits(png, 1u << 28, 1u << 28);  // width/height <= 2^28
  png_set_chunk_malloc_max(png, 1u << 28);       // per-chunk allocation cap
  png_set_chunk_cache_max(png, 1u << 16);        // chunk count cap

  ReadContext ctx{&source, 0};
  png_set_read_fn(png, &ctx, read_callback);

  png_read_info(png, info);

  ReferenceImage& image = result.image;
  image.width = static_cast<std::uint32_t>(png_get_image_width(png, info));
  image.height = static_cast<std::uint32_t>(png_get_image_height(png, info));
  image.source_bit_depth = static_cast<std::uint8_t>(png_get_bit_depth(png, info));
  image.source_color_type = static_cast<std::uint8_t>(png_get_color_type(png, info));
  image.interlaced = png_get_interlace_type(png, info) != PNG_INTERLACE_NONE;

  // Predict the post-transform channel count so png_set_filler can be applied
  // before the single required png_read_update_info call. After expand +
  // gray_to_rgb, alpha is present only for source RGBA, gray+alpha or tRNS.
  const int in_channels = png_get_channels(png, info);
  const bool has_trns = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
  const bool will_have_alpha = in_channels == 4 || in_channels == 2 || has_trns;

  // Documented transforms to a canonical RGBA8 delivery.
  png_set_expand(png);       // palette -> RGB, tRNS -> alpha, gray<8 -> 8-bit
  png_set_gray_to_rgb(png);  // gray -> RGB
  png_set_strip_16(png);     // 16-bit -> 8-bit
  if (!will_have_alpha) {
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);  // force RGBA stride
  }
  png_read_update_info(png, info);

  const png_size_t row_bytes = png_get_rowbytes(png, info);
  const std::uint64_t stride = static_cast<std::uint64_t>(row_bytes);
  if (stride != static_cast<std::uint64_t>(image.width) * 4) {
    png_destroy_read_struct(&png, &info, nullptr);
    result.error = "libpng: unexpected delivered row stride";
    return result;
  }

  const std::uint64_t total = stride * image.height;
  if (total > (1ull << 30)) {  // 1 GiB delivery cap
    png_destroy_read_struct(&png, &info, nullptr);
    result.error = "libpng: image too large to deliver";
    return result;
  }

  image.rgba.resize(static_cast<std::size_t>(total));
  std::vector<png_bytep> rows(image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    rows[y] = reinterpret_cast<png_bytep>(image.rgba.data() +
                                          static_cast<std::size_t>(y) *
                                              row_bytes);
  }
  png_read_image(png, rows.data());
  png_read_end(png, info);

  png_destroy_read_struct(&png, &info, nullptr);
  result.warnings = std::move(state.warnings);
  result.success = true;
  return result;
}

}  // namespace pnga::backend_libpng
