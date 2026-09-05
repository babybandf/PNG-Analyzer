#include <pnga/analysis-engine/document_fingerprint.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/io/byte_source.h>

namespace {

// Reference FNV-1a 64 implementation for differential comparison.
std::string reference_fnv1a64(const std::vector<std::byte>& data) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (std::byte value : data) {
    hash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(value));
    hash *= 0x100000001b3ULL;
  }
  std::string hex(16, '0');
  for (int i = 0; i < 16; ++i) {
    const unsigned nibble = static_cast<unsigned>((hash >> (60 - 4 * i)) & 0xFu);
    hex[static_cast<std::size_t>(i)] =
        static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
  }
  return "fnv1a64-v1:" + hex;
}

std::vector<std::byte> deterministic_pattern(std::size_t size) {
  std::vector<std::byte> data(size);
  std::uint64_t state = 0x9e3779b97f4a7c15ULL;
  for (std::size_t i = 0; i < size; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    data[i] = static_cast<std::byte>((state >> 33) & 0xFFu);
  }
  return data;
}

// A source that refuses view() so identity must work through read() windows.
class RefusingViewSource final : public pnga::io::IByteSource {
 public:
  explicit RefusingViewSource(std::vector<std::byte> data) noexcept
      : data_(std::move(data)) {}

  std::uint64_t size() const noexcept override { return data_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (offset > data_.size() || length > data_.size() - offset) {
      return false;
    }
    if (length != 0) {
      std::memcpy(out, data_.data() + offset, length);
    }
    return true;
  }
  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  std::vector<std::byte> data_;
};

// Requests cancellation while serving the first 64 KiB window read.
class CancelDuringFirstWindowSource final : public pnga::io::IByteSource {
 public:
  CancelDuringFirstWindowSource(std::uint64_t size,
                                pnga::analysis_engine::CancellationToken* token)
      : size_(size), token_(token) {}

  std::uint64_t size() const noexcept override { return size_; }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (offset > size_ || length > size_ - offset) {
      return false;
    }
    if (offset == 0 && token_ != nullptr) {
      token_->request_cancel();
    }
    std::memset(out, 0, length);
    return true;
  }
  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  std::uint64_t size_;
  pnga::analysis_engine::CancellationToken* token_;
};

// Serves only the first 64 KiB window and fails every later read.
class FailingSecondWindowSource final : public pnga::io::IByteSource {
 public:
  std::uint64_t size() const noexcept override { return 70000; }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (offset >= 65536) {
      return false;
    }
    std::memset(out, 0, length);
    return true;
  }
  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }
};

}  // namespace

TEST_CASE("Document identity fingerprints the source with FNV-1a 64",
          "[analysis-engine][wp602d]") {
  using pnga::analysis_engine::compute_document_identity;

  SECTION("empty source returns the offset basis") {
    const pnga::io::MemoryByteSource source(std::vector<std::byte>{});
    std::string error = "sentinel";
    const auto identity = compute_document_identity(source, nullptr, &error);
    REQUIRE(identity.has_value());
    REQUIRE(identity->file_size == 0);
    REQUIRE(identity->fingerprint == "fnv1a64-v1:cbf29ce484222325");
    REQUIRE(error.empty());
  }

  SECTION("canonical value for one byte a") {
    const pnga::io::MemoryByteSource source(std::vector<std::byte>{std::byte{'a'}});
    const auto identity = compute_document_identity(source, nullptr, nullptr);
    REQUIRE(identity.has_value());
    REQUIRE(identity->file_size == 1);
    REQUIRE(identity->fingerprint == "fnv1a64-v1:af63dc4c8601ec8c");
  }

  SECTION("multi-window equality across sources and the reference") {
    const auto data = deterministic_pattern(70000);
    const pnga::io::MemoryByteSource memory(data);
    const RefusingViewSource refusing(data);
    std::string error = "sentinel";
    const auto from_memory = compute_document_identity(memory, nullptr, &error);
    const auto from_refusing =
        compute_document_identity(refusing, nullptr, nullptr);
    REQUIRE(from_memory.has_value());
    REQUIRE(from_refusing.has_value());
    REQUIRE(from_memory->file_size == 70000);
    REQUIRE(from_memory->fingerprint == from_refusing->fingerprint);
    REQUIRE(from_memory->fingerprint == reference_fnv1a64(data));
    REQUIRE(error.empty());
  }

  SECTION("cancellation between 64 KiB reads yields no identity") {
    pnga::analysis_engine::CancellationToken token;
    const CancelDuringFirstWindowSource source(70000, &token);
    std::string error;
    const auto identity = compute_document_identity(source, &token, &error);
    REQUIRE_FALSE(identity.has_value());
    REQUIRE(error == "document fingerprint cancelled");
  }

  SECTION("pre-cancelled token yields no identity") {
    pnga::analysis_engine::CancellationToken token;
    token.request_cancel();
    const pnga::io::MemoryByteSource source(
        deterministic_pattern(10));
    std::string error;
    const auto identity = compute_document_identity(source, &token, &error);
    REQUIRE_FALSE(identity.has_value());
    REQUIRE(error == "document fingerprint cancelled");
  }

  SECTION("read failure yields no partial identity") {
    const FailingSecondWindowSource source;
    std::string error;
    const auto identity = compute_document_identity(source, nullptr, &error);
    REQUIRE_FALSE(identity.has_value());
    REQUIRE(error == "document fingerprint read failed");
  }
}
