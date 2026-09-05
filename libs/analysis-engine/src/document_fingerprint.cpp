// WP-602D: windowed FNV-1a 64 document fingerprint.

#include "pnga/analysis-engine/document_fingerprint.h"

#include <pnga/analysis-engine/job_scheduler.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pnga::analysis_engine {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr std::uint64_t kWindowBytes = 64 * 1024;

void append_hex16(std::string& out, std::uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  for (int shift = 60; shift >= 0; shift -= 4) {
    const auto nibble = static_cast<unsigned>((value >> shift) & 0xFu);
    out.push_back(kDigits[nibble]);
  }
}

}  // namespace

std::optional<pnga::statistics::DocumentIdentity> compute_document_identity(
    const pnga::io::IByteSource& source, const CancellationToken* cancellation,
    std::string* error) {
  const auto fail = [&](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return std::optional<pnga::statistics::DocumentIdentity>{};
  };

  pnga::statistics::DocumentIdentity identity;
  identity.file_size = source.size();

  std::uint64_t hash = kFnvOffsetBasis;
  std::uint64_t offset = 0;
  std::array<std::byte, kWindowBytes> window{};
  while (offset < identity.file_size) {
    const std::uint64_t remaining = identity.file_size - offset;
    const auto length = static_cast<std::size_t>(
        remaining < kWindowBytes ? remaining : kWindowBytes);
    if (length > remaining) {
      return fail("document fingerprint window length overflow");
    }
    if (cancellation != nullptr && cancellation->cancelled()) {
      return fail("document fingerprint cancelled");
    }
    if (!source.read(offset, window.data(), length)) {
      return fail("document fingerprint read failed");
    }
    for (std::size_t i = 0; i < length; ++i) {
      hash ^= static_cast<std::uint64_t>(
          std::to_integer<unsigned char>(window[i]));
      hash *= kFnvPrime;
    }
    offset += length;  // bounded by remaining, so no overflow is possible
  }

  identity.fingerprint = "fnv1a64-v1:";
  append_hex16(identity.fingerprint, hash);
  if (error != nullptr) {
    error->clear();
  }
  return identity;
}

}  // namespace pnga::analysis_engine
