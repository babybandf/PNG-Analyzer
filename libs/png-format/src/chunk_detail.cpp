// WP-5U8: bounded field decoding for the selected physical PNG Chunk.

#include "pnga/png-format/chunk_detail.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace pnga::png_format {

namespace {

constexpr std::uint64_t kMaxVariablePayload = 64 * 1024;
constexpr std::uint64_t kMaxTextValue = 4096;
constexpr std::uint64_t kMaxTransparencyEntries = 1024;
constexpr std::uint64_t kMaxPaletteBytes = 768;

unsigned int byte_value(std::byte value) noexcept {
  return std::to_integer<unsigned int>(value);
}

std::string number(std::uint64_t value) {
  std::array<char, 32> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    value);
  return std::string(buffer.data(), result.ptr);
}

std::string hex_number(std::uint64_t value) {
  std::array<char, 32> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    value, 16);
  std::string out("0x");
  for (const char* p = buffer.data(); p != result.ptr; ++p) {
    out.push_back(static_cast<char>(std::toupper(
        static_cast<unsigned char>(*p))));
  }
  return out;
}

std::string numeric(std::uint64_t value) {
  return number(value) + " (" + hex_number(value) + ")";
}

const char* color_type_name(unsigned int value) noexcept {
  switch (value) {
    case 0:
      return "Grayscale";
    case 2:
      return "Truecolor";
    case 3:
      return "Indexed-color";
    case 4:
      return "Grayscale with alpha";
    case 6:
      return "Truecolor with alpha";
    default:
      return "Unknown/reserved";
  }
}

std::string scaled(std::uint32_t value, std::uint32_t scale) {
  const std::uint32_t whole = value / scale;
  const std::uint32_t fraction = value % scale;
  std::string out = number(whole) + ".";
  std::array<char, 16> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    fraction);
  const std::size_t digits = static_cast<std::size_t>(result.ptr - buffer.data());
  const std::size_t width = scale == 100000 ? 5 : 3;
  out.append(width > digits ? width - digits : 0, '0');
  out.append(buffer.data(), result.ptr);
  while (out.size() > 2 && out.back() == '0') {
    out.pop_back();
  }
  if (out.back() == '.') {
    out.push_back('0');
  }
  return out;
}

std::uint16_t read_u16_be(const std::byte* data) noexcept {
  return static_cast<std::uint16_t>((byte_value(data[0]) << 8) |
                                     byte_value(data[1]));
}

std::uint32_t read_u32_be(const std::byte* data) noexcept {
  return (static_cast<std::uint32_t>(byte_value(data[0])) << 24) |
         (static_cast<std::uint32_t>(byte_value(data[1])) << 16) |
         (static_cast<std::uint32_t>(byte_value(data[2])) << 8) |
         static_cast<std::uint32_t>(byte_value(data[3]));
}

void add(ChunkDetail& detail, std::string name, std::string value) {
  detail.fields.push_back(
      ChunkDetailField{std::move(name), std::move(value)});
}

void add_basic(ChunkDetail& detail) {
  add(detail, "Data length", numeric(detail.data_length));
  add(detail, "Data offset", numeric(detail.data_offset));
  add(detail, "CRC offset", numeric(detail.crc_offset));
}

void add_error(ChunkDetail& detail, std::string value) {
  detail.basic_only = true;
  add(detail, "Payload", std::move(value));
}

std::optional<pnga::io::ByteView> bounded_view(
    const pnga::io::IByteSource& source, const ChunkNode& node,
    std::uint64_t maximum) {
  if (node.data_length > maximum ||
      node.data_length > static_cast<std::uint64_t>(
                              std::numeric_limits<std::size_t>::max())) {
    return std::nullopt;
  }
  return source.view(node.data_offset,
                     static_cast<std::size_t>(node.data_length));
}

std::string escaped_text(const std::byte* data, std::size_t length) {
  const std::size_t limit = std::min<std::size_t>(length, kMaxTextValue);
  std::string out;
  out.reserve(limit);
  for (std::size_t i = 0; i < limit; ++i) {
    const unsigned int value = byte_value(data[i]);
    if (value >= 0x20 && value <= 0x7E && value != '\\') {
      out.push_back(static_cast<char>(value));
    } else if (value == '\\') {
      out.append("\\\\");
    } else {
      out.append("\\x");
      const std::string hex = hex_number(value);
      if (hex.size() == 3) {
        out.push_back('0');
      }
      out.append(hex, 2, std::string::npos);
    }
  }
  if (length > limit) {
    out.append("…");
  }
  return out;
}

std::optional<std::size_t> nul_position(const pnga::io::ByteView& view,
                                        std::size_t start) {
  for (std::size_t i = start; i < view.size; ++i) {
    if (byte_value(view.data[i]) == 0) {
      return i;
    }
  }
  return std::nullopt;
}

void parse_ihdr(ChunkDetail& detail, const pnga::io::ByteView& view) {
  add(detail, "Width", numeric(read_u32_be(view.data)));
  add(detail, "Height", numeric(read_u32_be(view.data + 4)));
  add(detail, "Bit depth", numeric(byte_value(view.data[8])));
  const unsigned int color_type = byte_value(view.data[9]);
  add(detail, "Color type",
      numeric(color_type) + " (" + color_type_name(color_type) + ")");
  add(detail, "Compression method", numeric(byte_value(view.data[10])));
  add(detail, "Filter method", numeric(byte_value(view.data[11])));
  add(detail, "Interlace method", numeric(byte_value(view.data[12])));
  detail.basic_only = false;
}

void parse_plte(ChunkDetail& detail, const pnga::io::ByteView& view) {
  const std::size_t entries = view.size / 3;
  for (std::size_t i = 0; i < entries; ++i) {
    const std::size_t offset = i * 3;
    const std::string prefix = "Palette[" + number(i) + "] ";
    add(detail, prefix + "Red", numeric(byte_value(view.data[offset])));
    add(detail, prefix + "Green", numeric(byte_value(view.data[offset + 1])));
    add(detail, prefix + "Blue", numeric(byte_value(view.data[offset + 2])));
  }
  detail.basic_only = false;
}

void parse_trns(ChunkDetail& detail, const pnga::io::ByteView& view) {
  for (std::size_t i = 0; i < view.size; ++i) {
    add(detail, "Alpha[" + number(i) + "]", numeric(byte_value(view.data[i])));
  }
  detail.basic_only = false;
}

void parse_chrm(ChunkDetail& detail, const pnga::io::ByteView& view) {
  static constexpr std::array<const char*, 8> names = {
      "White point X", "White point Y", "Red X", "Red Y",
      "Green X",      "Green Y",      "Blue X", "Blue Y"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    add(detail, names[i], scaled(read_u32_be(view.data + i * 4), 100000));
  }
  detail.basic_only = false;
}

void parse_text(ChunkDetail& detail, const pnga::io::ByteView& view) {
  const auto separator = nul_position(view, 0);
  if (!separator.has_value()) {
    add_error(detail, "Malformed tEXt: missing keyword separator");
    return;
  }
  add(detail, "Keyword", escaped_text(view.data, *separator));
  add(detail, "Text", escaped_text(view.data + *separator + 1,
                                    view.size - *separator - 1));
  detail.basic_only = false;
}

void parse_ztxt(ChunkDetail& detail, const pnga::io::ByteView& view) {
  const auto separator = nul_position(view, 0);
  if (!separator.has_value() || *separator + 2 > view.size) {
    add_error(detail, "Malformed zTXt: missing keyword or compression method");
    return;
  }
  add(detail, "Keyword", escaped_text(view.data, *separator));
  add(detail, "Compression method", numeric(byte_value(view.data[*separator + 1])));
  add(detail, "Compressed text bytes",
      numeric(view.size - *separator - 2));
  detail.basic_only = false;
}

void parse_itxt(ChunkDetail& detail, const pnga::io::ByteView& view) {
  const auto keyword_end = nul_position(view, 0);
  if (!keyword_end.has_value() || *keyword_end + 2 >= view.size) {
    add_error(detail, "Malformed iTXt: missing keyword fields");
    return;
  }
  const std::size_t flag = *keyword_end + 1;
  const std::size_t method = flag + 1;
  const auto language_end = nul_position(view, method + 1);
  if (!language_end.has_value()) {
    add_error(detail, "Malformed iTXt: missing language separator");
    return;
  }
  const auto translated_end = nul_position(view, *language_end + 1);
  if (!translated_end.has_value()) {
    add_error(detail, "Malformed iTXt: missing translated-key separator");
    return;
  }
  add(detail, "Keyword", escaped_text(view.data, *keyword_end));
  add(detail, "Compression flag", numeric(byte_value(view.data[flag])));
  add(detail, "Compression method", numeric(byte_value(view.data[method])));
  add(detail, "Language", escaped_text(view.data + method + 1,
                                        *language_end - method - 1));
  add(detail, "Translated keyword",
      escaped_text(view.data + *language_end + 1,
                   *translated_end - *language_end - 1));
  add(detail, "Text", escaped_text(view.data + *translated_end + 1,
                                    view.size - *translated_end - 1));
  detail.basic_only = false;
}

}  // namespace

ChunkDetail describe_chunk(const pnga::io::IByteSource& source,
                           const ChunkNode& node) {
  ChunkDetail detail;
  detail.type = node.text();
  detail.data_length = node.data_length;
  detail.data_offset = node.data_offset;
  detail.crc_offset = node.crc_offset;
  add_basic(detail);

  if (detail.type == "IDAT") {
    add(detail, "Payload", "Compressed IDAT stream (not decoded here)");
    return detail;
  }
  if (detail.type == "IEND") {
    if (node.data_length == 0) {
      add(detail, "Payload", "Empty");
    } else {
      add_error(detail, "Malformed IEND: payload must be empty");
    }
    return detail;
  }

  const auto fixed = [&](std::uint64_t expected,
                         auto parser) {
    if (node.data_length != expected) {
      add_error(detail, "Malformed " + detail.type + ": expected " +
                              number(expected) + " bytes");
      return;
    }
    const auto view = bounded_view(source, node, expected);
    if (!view.has_value() || view->size != expected) {
      add_error(detail, "Payload is not readable");
      return;
    }
    parser(*view);
  };

  if (detail.type == "IHDR") {
    fixed(13, [&](const auto& view) { parse_ihdr(detail, view); });
  } else if (detail.type == "PLTE") {
    if (node.data_length == 0 || node.data_length > kMaxPaletteBytes ||
        node.data_length % 3 != 0) {
      add_error(detail, "Malformed PLTE: length must be a non-zero multiple of 3");
    } else if (const auto view = bounded_view(source, node, kMaxPaletteBytes);
               view.has_value()) {
      parse_plte(detail, *view);
    } else {
      add_error(detail, "Payload is not readable");
    }
  } else if (detail.type == "tRNS") {
    if (node.data_length == 0 || node.data_length > kMaxTransparencyEntries) {
      add_error(detail, "Malformed or oversized tRNS payload");
    } else if (const auto view = bounded_view(source, node, kMaxTransparencyEntries);
               view.has_value()) {
      parse_trns(detail, *view);
    } else {
      add_error(detail, "Payload is not readable");
    }
  } else if (detail.type == "cHRM") {
    fixed(32, [&](const auto& view) { parse_chrm(detail, view); });
  } else if (detail.type == "gAMA") {
    fixed(4, [&](const auto& view) {
      add(detail, "Gamma", scaled(read_u32_be(view.data), 100000));
      detail.basic_only = false;
    });
  } else if (detail.type == "sRGB") {
    fixed(1, [&](const auto& view) {
      static constexpr std::array<const char*, 4> intents = {
          "Perceptual", "Relative colorimetric", "Saturation",
          "Absolute colorimetric"};
      const unsigned int intent = byte_value(view.data[0]);
      const std::string label = intent < intents.size() ? intents[intent]
                                                         : "Unknown";
      add(detail, "Rendering intent", numeric(intent) + " (" + label + ")");
      detail.basic_only = false;
    });
  } else if (detail.type == "pHYs") {
    fixed(9, [&](const auto& view) {
      add(detail, "Pixels per unit X", numeric(read_u32_be(view.data)));
      add(detail, "Pixels per unit Y", numeric(read_u32_be(view.data + 4)));
      add(detail, "Unit", byte_value(view.data[8]) == 1 ? "Meter" :
                            numeric(byte_value(view.data[8])) + " (unspecified)");
      detail.basic_only = false;
    });
  } else if (detail.type == "tIME") {
    fixed(7, [&](const auto& view) {
      const std::uint16_t year = read_u16_be(view.data);
      add(detail, "Timestamp", number(year) + "-" + number(byte_value(view.data[2])) +
                                      "-" + number(byte_value(view.data[3])) +
                                      " " + number(byte_value(view.data[4])) +
                                      ":" + number(byte_value(view.data[5])) +
                                      ":" + number(byte_value(view.data[6])));
      detail.basic_only = false;
    });
  } else if (detail.type == "tEXt" || detail.type == "zTXt" ||
             detail.type == "iTXt") {
    const auto view = bounded_view(source, node, kMaxVariablePayload);
    if (!view.has_value()) {
      add_error(detail, "Payload is oversized or not readable");
    } else if (detail.type == "tEXt") {
      parse_text(detail, *view);
    } else if (detail.type == "zTXt") {
      parse_ztxt(detail, *view);
    } else {
      parse_itxt(detail, *view);
    }
  } else {
    add_error(detail, "Chunk schema not implemented; use Hex view for payload");
  }
  return detail;
}

}  // namespace pnga::png_format
