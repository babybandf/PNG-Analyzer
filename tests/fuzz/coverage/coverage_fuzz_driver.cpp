// WP-603D: optional LLVM libFuzzer parser/stream harness. The build is
// disabled by default because the fuzzer runtime is compiler-provided.

#include <pnga/analysis-engine/validation.h>
#include <pnga/deflate-trace/zlib_wrapper.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                       std::size_t size) {
  constexpr std::size_t kMaxInputBytes = 1U << 20;
  if (data == nullptr || size > kMaxInputBytes) {
    return 0;
  }
  std::vector<std::byte> bytes(size);
  for (std::size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<std::byte>(data[i]);
  }
  pnga::io::MemoryByteSource source(bytes);
  const auto index = pnga::png_format::index_chunks(source);
  (void)pnga::analysis_engine::validate_document(source, index);
  const pnga::png_format::VirtualIDATStream stream(index);
  if (stream.size() != 0) {
    const std::size_t take = static_cast<std::size_t>(
        std::min<std::uint64_t>(stream.size(), 256));
    std::vector<std::byte> window(take);
    (void)stream.read(source, 0, window.data(), window.size());
  }
  (void)pnga::deflate_trace::trace_zlib_wrapper(source);
  return 0;
}
