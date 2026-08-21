// WP-500 zlib wrapper trace implementation. Pure byte parsing: CMF/FLG
// sub-fields, optional DICTID and the trailing Adler-32, each exposed as a
// trace-model field with a bit span. No zlib is linked here (layout §7).

#include "pnga/deflate-trace/zlib_wrapper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace pnga::deflate_trace {

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

std::uint32_t u32_be(const std::byte* p) {
  return (static_cast<std::uint32_t>(u8(p[0])) << 24) |
         (static_cast<std::uint32_t>(u8(p[1])) << 16) |
         (static_cast<std::uint32_t>(u8(p[2])) << 8) |
         static_cast<std::uint32_t>(u8(p[3]));
}

// Sub-byte span within byte `byte_offset`, bits `bit_offset`..`bit_offset+n`.
pnga::trace_model::BitSpan bit_span(std::uint64_t byte_offset,
                                    unsigned bit_offset, unsigned bits) {
  pnga::trace_model::BitSpan span;
  span.offset = byte_offset;
  span.length = bits;
  span.bit_offset = static_cast<std::uint8_t>(bit_offset);
  span.bit_aligned = true;
  return span;
}

// Whole-byte span starting at `byte_offset` of `bytes` bytes.
pnga::trace_model::BitSpan byte_span(std::uint64_t byte_offset,
                                     std::uint64_t bytes) {
  pnga::trace_model::BitSpan span;
  span.offset = byte_offset;
  span.length = bytes;
  span.bit_aligned = false;
  return span;
}

void push_field(std::vector<WrapperField>& fields, pnga::trace_model::BitSpan span,
                const char* name, std::uint64_t value, std::string text) {
  fields.push_back(WrapperField{span, name, std::move(text), value});
}

}  // namespace

ZlibWrapperTrace trace_zlib_wrapper(const pnga::io::IByteSource& source) {
  ZlibWrapperTrace out;
  out.total_bytes = source.size();

  if (source.size() < 2) {
    out.error = "stream too short for a zlib header";
    return out;
  }
  std::byte head[2] = {};
  source.read(0, head, 2);
  out.cmf = u8(head[0]);
  out.flg = u8(head[1]);
  out.cm = static_cast<std::uint8_t>(out.cmf & 0x0Fu);
  out.cinfo = static_cast<std::uint8_t>((out.cmf >> 4) & 0x0Fu);
  out.fcheck_ok = ((static_cast<std::uint32_t>(out.cmf) << 8 |
                    static_cast<std::uint32_t>(out.flg)) %
                   31u) == 0;
  out.fdict = (out.flg & 0x20u) != 0;
  out.flevel = static_cast<std::uint8_t>((out.flg >> 6) & 0x03u);

  out.fields.reserve(10);
  push_field(out.fields, bit_span(0, 0, 4), "CM", out.cm,
             out.cm == 8 ? "deflate" : "unknown");
  push_field(out.fields, bit_span(0, 4, 4), "CINFO", out.cinfo,
             "window 2^" + std::to_string(out.cinfo + 8));
  push_field(out.fields, bit_span(1, 0, 5), "FCHECK", out.flg & 0x1Fu,
             out.fcheck_ok ? "ok" : "bad");
  push_field(out.fields, bit_span(1, 5, 1), "FDICT", out.fdict ? 1 : 0,
             out.fdict ? "set" : "unset");
  push_field(out.fields, bit_span(1, 6, 2), "FLEVEL", out.flevel,
             out.flevel == 0 ? "fastest" : out.flevel == 3 ? "max" : "default");

  if (out.cm != 8) {
    out.error = "compression method is not deflate";
    return out;
  }
  if (!out.fcheck_ok) {
    out.error = "zlib header check fails (FCHECK)";
    return out;
  }
  if (out.cinfo > 7) {
    out.error = "unsupported window size (CINFO)";
    return out;
  }

  std::uint64_t cursor = 2;
  if (out.fdict) {
    if (source.size() < 6) {
      out.error = "FDICT set but DICTID is missing";
      return out;
    }
    std::byte id[4] = {};
    source.read(2, id, 4);
    out.dictid = u32_be(id);
    push_field(out.fields, byte_span(2, 4), "DICTID", *out.dictid,
               "0x" + [&] {
                 char buf[16];
                 std::snprintf(buf, sizeof(buf), "%08x", *out.dictid);
                 return std::string(buf);
               }());
    cursor = 6;
  }
  out.deflate_data_begin = cursor;

  if (source.size() < cursor + 4) {
    out.error = "stream too short for the trailing Adler-32";
    return out;
  }
  const std::uint64_t adler_off = source.size() - 4;
  std::byte a[4] = {};
  source.read(adler_off, a, 4);
  out.adler_offset = adler_off;
  out.adler_value = u32_be(a);
  push_field(out.fields, byte_span(adler_off, 4), "ADLER32", *out.adler_value,
             "0x" + [&] {
               char buf[16];
               std::snprintf(buf, sizeof(buf), "%08x", *out.adler_value);
               return std::string(buf);
             }());

  out.success = true;
  return out;
}

}  // namespace pnga::deflate_trace
