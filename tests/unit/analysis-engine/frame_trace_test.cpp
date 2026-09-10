// WP-APNG-INSPECT T04: per-frame compression evidence and physical source
// mapping (contract C2). The TraceOrchestrator opens an AnalysisTarget
// without rescanning the file; compose_trace_query reuses the static
// kernel over the frame stream; token/Adler/Huffman facts survive fdAT
// slicing; boundary bytes outside frame payload spans do not reverse-map.

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/trace_orchestrator.h>
#include <pnga/analysis-engine/trace_query.h>

#include <pnga/deflate-index/block_index.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/trace-model/selection.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "apng_fixture.h"
#include "apng_inspection_fixture.h"
#include "test_png_helpers.h"

using namespace pnga::analysis_engine;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::FrameControl;
using pnga::png_format::VirtualIDATStream;
using pnga::png_format::index_chunks;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::Selection;

namespace {

// Returns the byte offset of the first fdAT chunk body and its chunk
// ordinal (0-based over all chunks, signature excluded).
std::size_t find_fdAT_chunk(const std::vector<std::byte>& png,
                            std::size_t* ordinal_out) {
  std::uint64_t pos = pnga::png_format::kPngSignature.size();
  for (std::size_t ordinal = 0; pos + 12 <= png.size(); ++ordinal) {
    const std::uint32_t length = pnga_test::fixture_u32(png.data() + pos);
    const char* type = reinterpret_cast<const char*>(png.data() + pos + 4);
    if (type[0] == 'f' && type[1] == 'd' && type[2] == 'A' &&
        type[3] == 'T') {
      *ordinal_out = ordinal;
      return static_cast<std::size_t>(pos + 8);
    }
    pos += 12 + length;
  }
  return 0;
}

// Test-local adapter mirroring the production VirtualIdatSource bridge: a
// VirtualIDATStream is consumed through a separate file source.
class StaticIdatSource final : public pnga::io::IByteSource {
 public:
  StaticIdatSource(const VirtualIDATStream& stream,
                   const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}
  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

// Wraps a zlib payload as a one-frame APNG (fdAT frame 0 at (0,0) on a 1x1
// canvas) and as a static PNG (IDAT), sharing the exact same payload bytes.
struct ZlibWrap {
  std::vector<std::byte> static_png;
  std::vector<std::byte> apng;
};

ZlibWrap wrap_zlib_payload(const std::vector<std::byte>& zlib_payload,
                           std::uint32_t width = 1, std::uint32_t height = 1) {
  ZlibWrap out;
  const auto ihdr = [](std::uint32_t w, std::uint32_t h) {
    std::vector<std::byte> data;
    pnga_test::append_u32(data, w);
    pnga_test::append_u32(data, h);
    data.push_back(pnga_test::B(8));   // bit depth
    data.push_back(pnga_test::B(0));   // grayscale
    data.push_back(pnga_test::B(0));   // compression
    data.push_back(pnga_test::B(0));   // filter
    data.push_back(pnga_test::B(0));   // interlace
    return data;
  };

  out.static_png.insert(out.static_png.end(),
                        pnga::png_format::kPngSignature.begin(),
                        pnga::png_format::kPngSignature.end());
  pnga_test::append_apng_chunk(out.static_png, "IHDR", ihdr(width, height));
  pnga_test::append_apng_chunk(out.static_png, "IDAT", zlib_payload);
  pnga_test::append_apng_chunk(out.static_png, "IEND", {});

  out.apng.insert(out.apng.end(), pnga::png_format::kPngSignature.begin(),
                  pnga::png_format::kPngSignature.end());
  pnga_test::append_apng_chunk(out.apng, "IHDR", ihdr(width, height));
  std::vector<std::byte> actl;
  pnga_test::append_u32(actl, 1);
  pnga_test::append_u32(actl, 0);
  pnga_test::append_apng_chunk(out.apng, "acTL", actl);
  std::vector<std::byte> fctl;
  pnga_test::append_u32(fctl, 0);  // sequence
  pnga_test::append_u32(fctl, width);
  pnga_test::append_u32(fctl, height);
  pnga_test::append_u32(fctl, 0);
  pnga_test::append_u32(fctl, 0);
  pnga_test::append_u16(fctl, 1);
  pnga_test::append_u16(fctl, 100);
  fctl.push_back(pnga_test::B(0));  // dispose
  fctl.push_back(pnga_test::B(0));  // blend
  pnga_test::append_apng_chunk(out.apng, "fcTL", fctl);
  std::vector<std::byte> fdat;
  pnga_test::append_u32(fdat, 1);  // sequence follows fcTL
  fdat.insert(fdat.end(), zlib_payload.begin(), zlib_payload.end());
  pnga_test::append_apng_chunk(out.apng, "fdAT", fdat);
  pnga_test::append_apng_chunk(out.apng, "IEND", {});
  return out;
}

FrameRequest frame_request_for(const std::vector<std::byte>& png,
                               std::uint32_t ordinal = 0) {
  auto source = std::make_shared<const MemoryByteSource>(png);
  FrameRequest request;
  request.generation = 7;
  request.request_serial = 11;
  request.ordinal = ordinal;
  request.source = source;
  request.index = std::make_shared<const pnga::png_format::AnimationIndex>(
      pnga::png_format::index_animation(*source,
                                        pnga::png_format::AnimationLimits{},
                                        [] { return false; }));
  request.canvas_header = pnga::png_reconstruction::ImageHeader{1, 1, 8, 0,
                                                                false};
  return request;
}

Selection trace_selection(std::uint32_t frame_index = 0) {
  Selection selection;
  selection.stage = pnga::trace_model::Stage::kTrace;
  pnga::trace_model::ImageCoordinate coordinate;
  coordinate.identity = AnimationFrame{frame_index};
  selection.image = coordinate;
  return selection;
}

// Stored-block zlib payload over `raw` (single BFINAL stored block).
std::vector<std::byte> stored_zlib(const std::vector<std::byte>& raw) {
  std::vector<std::byte> payload;
  payload.push_back(pnga_test::B(0x78));
  payload.push_back(pnga_test::B(0x01));
  payload.push_back(pnga_test::B(0x01));  // BFINAL=1, BTYPE=00, aligned
  const std::uint16_t len = static_cast<std::uint16_t>(raw.size());
  const std::uint16_t nlen = static_cast<std::uint16_t>(~len & 0xFFFFu);
  payload.push_back(pnga_test::B(static_cast<unsigned char>(len & 0xFF)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(len >> 8)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(nlen & 0xFF)));
  payload.push_back(pnga_test::B(static_cast<unsigned char>(nlen >> 8)));
  payload.insert(payload.end(), raw.begin(), raw.end());
  const uLong adler =
      adler32(adler32(0, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(raw.data()),
              static_cast<uInt>(raw.size()));
  for (int shift = 24; shift >= 0; shift -= 8) {
    payload.push_back(pnga_test::B(
        static_cast<unsigned char>((adler >> shift) & 0xFFu)));
  }
  return payload;
}

// Fixed-Huffman zlib payload over "ABC" (final fixed block + 7-bit EOB).
std::vector<std::byte> fixed_abc_zlib() {
  const std::vector<std::byte> output = {pnga_test::B(0x41),
                                         pnga_test::B(0x42),
                                         pnga_test::B(0x43)};
  std::vector<std::byte> zlib = {pnga_test::B(0x78), pnga_test::B(0x01),
                                 pnga_test::B(0x73), pnga_test::B(0x74),
                                 pnga_test::B(0x72), pnga_test::B(0x06),
                                 pnga_test::B(0x00)};
  const uLong adler =
      adler32(adler32(0, Z_NULL, 0),
              reinterpret_cast<const Bytef*>(output.data()),
              static_cast<uInt>(output.size()));
  for (int shift = 24; shift >= 0; shift -= 8) {
    zlib.push_back(pnga_test::B(
        static_cast<unsigned char>((adler >> shift) & 0xFFu)));
  }
  return zlib;
}

}  // namespace

TEST_CASE("Trace orchestrator opens a frame target without rescanning",
          "[apng-inspect]") {
  auto t = make_frame_target(pnga_test::inspection_request());
  REQUIRE(t.target);
  TraceOrchestrator orchestrator(1, 1u << 20);
  REQUIRE(orchestrator.open(t.target, 1u << 20));
  REQUIRE(orchestrator.has_index());
  REQUIRE(orchestrator.queued_tasks() == 0);
  REQUIRE(orchestrator.document_generation() == 7);

  std::vector<pnga::png_format::PhysicalRange> spans;
  REQUIRE(t.target->stream->logical_to_physical(0, t.target->stream->size(),
                                                spans));
  REQUIRE_FALSE(spans.empty());
  REQUIRE(orchestrator.fast_index().status ==
          FastCompressionIndexStatus::kReady);
  REQUIRE(orchestrator.fast_index().generation == 7);
}

TEST_CASE("Frame trace preserves token evidence across fdAT slicing",
          "[apng-inspect]") {
  auto t = make_frame_target(pnga_test::inspection_request());
  REQUIRE(t.target);
  auto block_index =
      pnga::deflate_index::index_blocks(*t.target->stream, 1u << 20);
  REQUIRE(block_index.success);
  auto trace =
      pnga::deflate_trace::decode_stored_and_fixed(*t.target->stream, 1u << 20);
  REQUIRE(trace.success);

  const auto result = compose_trace_query(
      7, trace_selection(0), block_index, trace, *t.target->stream, 0,
      trace.output_bytes, 100000);
  REQUIRE(result.status == TraceQueryStatus::kReady);
  REQUIRE_FALSE(result.tokens.empty());

  // Every logical input byte envelope maps into the frame payload physical
  // spans; each span lies fully inside an fdAT data area of the source.
  REQUIRE_FALSE(result.physical_input.empty());
  std::vector<pnga::png_format::PhysicalRange> payload_spans;
  REQUIRE(t.target->stream->logical_to_physical(0, t.target->stream->size(),
                                                payload_spans));
  for (const auto& span : result.physical_input) {
    bool contained = false;
    for (const auto& payload : payload_spans) {
      if (span.offset >= payload.offset &&
          span.offset + span.length <= payload.offset + payload.length) {
        contained = true;
        break;
      }
    }
    REQUIRE(contained);
  }

  // Reverse mapping fails on the fdAT sequence-number byte and inside the
  // CRC bytes; the payload end itself maps to the stream end by design.
  const auto& first = payload_spans.front();
  REQUIRE_FALSE(
      t.target->stream->physical_to_logical(first.offset - 1).has_value());
  REQUIRE_FALSE(t.target->stream
                    ->physical_to_logical(first.offset + first.length + 1)
                    .has_value());
}

TEST_CASE("Same logical offsets in different frames stay independent",
          "[apng-inspect]") {
  // Two same-size frames with different pixel payloads.
  std::vector<std::byte> apng;
  apng.insert(apng.end(), pnga::png_format::kPngSignature.begin(),
              pnga::png_format::kPngSignature.end());
  std::vector<std::byte> ihdr;
  pnga_test::append_u32(ihdr, 2);
  pnga_test::append_u32(ihdr, 3);
  ihdr.push_back(pnga_test::B(8));
  ihdr.push_back(pnga_test::B(6));
  ihdr.push_back(pnga_test::B(0));
  ihdr.push_back(pnga_test::B(0));
  ihdr.push_back(pnga_test::B(0));
  pnga_test::append_apng_chunk(apng, "IHDR", ihdr);
  std::vector<std::byte> actl;
  pnga_test::append_u32(actl, 2);
  pnga_test::append_u32(actl, 0);
  pnga_test::append_apng_chunk(apng, "acTL", actl);
  const auto payload_for = [](std::uint8_t salt) {
    std::vector<std::byte> raw;
    for (std::uint32_t y = 0; y < 3; ++y) {
      raw.push_back(std::byte{0});  // filter None
      for (std::uint32_t x = 0; x < 2; ++x) {
        for (int c = 0; c < 4; ++c) {
          raw.push_back(std::byte{static_cast<unsigned char>(
              (x * 7 + y * 13 + c * 977 + 5 + salt) % 256)});
        }
      }
    }
    return pnga_test::zlib_deflate(raw);
  };
  std::uint32_t sequence = 0;
  const auto append_frame = [&](std::uint8_t salt) {
    std::vector<std::byte> fctl;
    pnga_test::append_u32(fctl, sequence++);
    pnga_test::append_u32(fctl, 2);
    pnga_test::append_u32(fctl, 3);
    pnga_test::append_u32(fctl, 0);
    pnga_test::append_u32(fctl, 0);
    pnga_test::append_u16(fctl, 1);
    pnga_test::append_u16(fctl, 100);
    fctl.push_back(pnga_test::B(0));
    fctl.push_back(pnga_test::B(0));
    pnga_test::append_apng_chunk(apng, "fcTL", fctl);
    const auto payload = payload_for(salt);
    std::vector<std::byte> fdat;
    pnga_test::append_u32(fdat, sequence++);
    fdat.insert(fdat.end(), payload.begin(), payload.end());
    pnga_test::append_apng_chunk(apng, "fdAT", fdat);
  };
  append_frame(0);
  append_frame(200);
  pnga_test::append_apng_chunk(apng, "IEND", {});

  const auto trace_of = [&](std::uint32_t ordinal) {
    auto request = frame_request_for(apng, ordinal);
    request.canvas_header = pnga::png_reconstruction::ImageHeader{2, 3, 8, 6,
                                                                  false};
    auto t = make_frame_target(request);
    REQUIRE(t.target);
    auto block_index =
        pnga::deflate_index::index_blocks(*t.target->stream, 1u << 20);
    REQUIRE(block_index.success);
    auto trace = pnga::deflate_trace::decode_stored_and_fixed(*t.target->stream,
                                                              1u << 20);
    REQUIRE(trace.success);
    auto result = compose_trace_query(7, trace_selection(ordinal),
                                      block_index, trace, *t.target->stream, 0,
                                      trace.output_bytes, 100000);
    REQUIRE(result.status == TraceQueryStatus::kReady);
    return result;
  };

  const auto frame0 = trace_of(0);
  const auto frame1 = trace_of(1);
  REQUIRE(frame0.tokens.size() == frame1.tokens.size());
  REQUIRE_FALSE(frame0.tokens.empty());
  // Identical logical offsets, different bytes: the token input spans match
  // in shape but the inflated output content cannot be shared.
  REQUIRE(frame0.inflated_end == frame1.inflated_end);
  bool differs = false;
  for (std::size_t i = 0; i < frame0.tokens.size(); ++i) {
    REQUIRE(frame0.tokens[i].output_begin == frame1.tokens[i].output_begin);
    REQUIRE(frame0.tokens[i].output_end == frame1.tokens[i].output_end);
    if (frame0.tokens[i].literal != frame1.tokens[i].literal) {
      differs = true;
    }
  }
  REQUIRE(differs);
}

TEST_CASE("Stored, fixed and dynamic payloads survive frame wrapping",
          "[apng-inspect]") {
  const std::vector<std::byte> stored_raw = {pnga_test::B(0),
                                             pnga_test::B(5)};
  const auto stored = stored_zlib(stored_raw);
  const auto fixed = fixed_abc_zlib();
  // A larger, poorly compressible payload makes zlib emit dynamic blocks.
  std::vector<std::byte> dynamic_raw;
  dynamic_raw.push_back(pnga_test::B(0));
  for (int i = 0; i < 4096; ++i) {
    dynamic_raw.push_back(pnga_test::B(static_cast<unsigned char>(
        (i * 31 + (i / 7) * 17 + (i % 13) * 3) % 251)));
  }
  const auto dynamic = pnga_test::zlib_deflate(dynamic_raw);

  const auto tokens_of = [&](const std::vector<std::byte>& payload,
                             pnga::deflate_index::BlockType expected_type) {
    const auto wrapped = wrap_zlib_payload(payload);
    auto request = frame_request_for(wrapped.apng);
    auto t = make_frame_target(request);
    REQUIRE(t.target);
    auto block_index =
        pnga::deflate_index::index_blocks(*t.target->stream, 1u << 20);
    REQUIRE(block_index.success);
    REQUIRE_FALSE(block_index.blocks.empty());
    bool type_found = false;
    for (const auto& block : block_index.blocks) {
      if (block.type == expected_type) {
        type_found = true;
      }
    }
    REQUIRE(type_found);
    auto trace = pnga::deflate_trace::decode_stored_and_fixed(*t.target->stream,
                                                              1u << 20);
    REQUIRE(trace.success);
    auto frame_result = compose_trace_query(7, trace_selection(0),
                                            block_index, trace,
                                            *t.target->stream, 0,
                                            trace.output_bytes, 100000);
    REQUIRE(frame_result.status == TraceQueryStatus::kReady);
    REQUIRE_FALSE(frame_result.tokens.empty());

    // Static wrapper over the same payload must produce the same logical
    // tokens and Huffman table facts.
    MemoryByteSource file(wrapped.static_png);
    const ChunkIndex chunks = index_chunks(file);
    const VirtualIDATStream stream(chunks);
    const StaticIdatSource logical(stream, file);
    auto static_blocks =
        pnga::deflate_index::index_blocks(logical, 1u << 20);
    REQUIRE(static_blocks.success);
    auto static_trace =
        pnga::deflate_trace::decode_stored_and_fixed(logical, 1u << 20);
    REQUIRE(static_trace.success);
    auto static_result = compose_trace_query(
        7, Selection{}, static_blocks, static_trace, stream, file, 0,
        static_trace.output_bytes, 100000);
    REQUIRE(static_result.status == TraceQueryStatus::kReady);
    REQUIRE(frame_result.tokens.size() == static_result.tokens.size());
    for (std::size_t i = 0; i < frame_result.tokens.size(); ++i) {
      REQUIRE(frame_result.tokens[i].output_begin ==
              static_result.tokens[i].output_begin);
      REQUIRE(frame_result.tokens[i].output_end ==
              static_result.tokens[i].output_end);
      REQUIRE(frame_result.tokens[i].literal ==
              static_result.tokens[i].literal);
    }
    return frame_result;
  };

  const auto stored_tokens = tokens_of(stored, pnga::deflate_index::BlockType::kStored);
  const auto fixed_tokens = tokens_of(fixed, pnga::deflate_index::BlockType::kFixed);
  const auto dynamic_tokens =
      tokens_of(dynamic, pnga::deflate_index::BlockType::kDynamic);
  REQUIRE(stored_tokens.tokens.size() == 3);  // 2 stored-byte literals + EOB
  REQUIRE(fixed_tokens.tokens.size() == 4);   // 3 literals + EOB
  REQUIRE_FALSE(dynamic_tokens.tokens.empty());
}

TEST_CASE("Broken Adler and truncated frame payloads stay partial or error",
          "[apng-inspect]") {
  const auto payload_outcome = [](std::vector<std::byte> payload,
                                  bool corrupt_adler) {
    if (corrupt_adler) {
      payload.back() = payload.back() ^ std::byte{0xff};  // wrong Adler
    } else {
      payload.pop_back();  // truncated stream
    }
    auto wrapped = wrap_zlib_payload(payload);
    std::size_t fdat_ordinal = 0;
    const std::size_t body = find_fdAT_chunk(wrapped.apng, &fdat_ordinal);
    REQUIRE(body != 0);
    pnga_test::refresh_chunk_crc(wrapped.apng, fdat_ordinal);
    auto request = frame_request_for(wrapped.apng);
    auto t = make_frame_target(request);
    REQUIRE(t.target);
    auto block_index =
        pnga::deflate_index::index_blocks(*t.target->stream, 1u << 20);
    auto trace =
        pnga::deflate_trace::decode_stored_and_fixed(*t.target->stream,
                                                     1u << 20);
    return std::pair{std::move(block_index), std::move(trace)};
  };

  // Wrong Adler: the fast index reports the mismatch instead of silently
  // presenting verified facts.
  {
    auto [block_index, trace] =
        payload_outcome(stored_zlib({pnga_test::B(0), pnga_test::B(5)}), true);
    REQUIRE(block_index.adler.status ==
            pnga::deflate_index::Adler32Status::kMismatch);
    REQUIRE_FALSE(block_index.success);
  }
  // Truncated stream: the decode stays partial (stream not ended) and the
  // fast index does not claim a verified complete stream.
  {
    auto [block_index, trace] =
        payload_outcome(stored_zlib({pnga_test::B(0), pnga_test::B(5)}),
                        false);
    REQUIRE_FALSE(trace.stream_ended);
    REQUIRE_FALSE(block_index.success);
  }
}

TEST_CASE("Frame trace rejects foreign selection identities", "[apng-inspect]") {
  auto t = make_frame_target(pnga_test::inspection_request());
  REQUIRE(t.target);
  TraceOrchestrator orchestrator(1, 1u << 20);
  REQUIRE(orchestrator.open(t.target, 1u << 20));

  TraceOrchestrationRequest request;
  request.generation = orchestrator.document_generation();
  request.selection = trace_selection(0);
  request.inflated_end = 1;
  request.max_tokens = 100;
  request.trace_output_budget_bytes = 1u << 20;
  REQUIRE(orchestrator.submit(request).status == TraceSubmitStatus::kQueued);

  TraceOrchestrationRequest foreign = request;
  foreign.selection = trace_selection(1);
  const auto rejected = orchestrator.submit(foreign);
  REQUIRE(rejected.status == TraceSubmitStatus::kRejected);
  REQUIRE(rejected.error == "selection identity does not match the open target");
}
