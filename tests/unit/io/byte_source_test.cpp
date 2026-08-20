// WP-100 ByteSource unit tests: bounds, checked arithmetic, zero-copy views,
// mmap-backed file access and the synthetic >4 GiB sizing contract.

#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

using pnga::io::ByteView;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::io::MappedFileByteSource;
using pnga::io::open_mapped_file;

namespace {

std::vector<std::byte> bytes_of(std::initializer_list<unsigned char> list) {
  std::vector<std::byte> out;
  out.reserve(list.size());
  for (unsigned char c : list) {
    out.push_back(static_cast<std::byte>(c));
  }
  return out;
}

std::vector<std::byte> make_pattern(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(i & 0xFF));
  }
  return out;
}

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

// Test double claiming a >4 GiB size with no backing storage. Proves that
// size() is 64-bit and that huge offsets are rejected without wrapping.
class FakeHugeSource final : public IByteSource {
 public:
  explicit FakeHugeSource(std::uint64_t size) : size_(size) {}
  std::uint64_t size() const noexcept override { return size_; }
  bool read(std::uint64_t, std::byte*, std::size_t) const noexcept override {
    return false;
  }
  std::optional<ByteView> view(std::uint64_t,
                               std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  std::uint64_t size_;
};

// Deterministic temp file for the mmap tests (no time/random dependence).
std::filesystem::path test_file_path() {
  return std::filesystem::temp_directory_path() / "pnga_bytesource_test.bin";
}

void write_file(const std::filesystem::path& path,
                const std::vector<std::byte>& data) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  os.write(reinterpret_cast<const char*>(data.data()),
           static_cast<std::streamsize>(data.size()));
}

}  // namespace

TEST_CASE("MemoryByteSource empty file has zero size and rejects reads",
          "[io][bytesource]") {
  MemoryByteSource src{{}};
  REQUIRE(src.size() == 0);
  std::byte out = std::byte{0xAB};
  REQUIRE(src.read(0, &out, 0));  // zero-length read at offset 0 is valid
  REQUIRE_FALSE(src.read(0, &out, 1));
  REQUIRE_FALSE(src.read(1, &out, 0));  // offset 1 outside a 0-size source
  auto v = src.view(0, 0);
  REQUIRE(v.has_value());
  REQUIRE(v->empty());
  REQUIRE_FALSE(src.view(0, 1).has_value());
}

TEST_CASE("MemoryByteSource reads and views a one-byte file", "[io][bytesource]") {
  MemoryByteSource src(bytes_of({0x5A}));
  REQUIRE(src.size() == 1);

  std::array<std::byte, 1> buf{};
  REQUIRE(src.read(0, buf.data(), 1));
  REQUIRE(buf[0] == std::byte{0x5A});
  REQUIRE(src.read(1, buf.data(), 0));  // empty read exactly at EOF

  auto v = src.view(0, 1);
  REQUIRE(v.has_value());
  REQUIRE(v->size == 1);
  REQUIRE(v->data[0] == std::byte{0x5A});

  REQUIRE_FALSE(src.read(1, buf.data(), 1));  // 1 byte past EOF
  REQUIRE_FALSE(src.view(1, 1).has_value());
  REQUIRE_FALSE(src.view(0, 2).has_value());
}

TEST_CASE("MemoryByteSource partial ranges stay in bounds", "[io][bytesource]") {
  const auto data = make_pattern(16);
  MemoryByteSource src(data);

  std::array<std::byte, 4> buf{};
  REQUIRE(src.read(4, buf.data(), 4));
  for (int i = 0; i < 4; ++i) {
    REQUIRE(buf[static_cast<std::size_t>(i)] ==
            static_cast<std::byte>(static_cast<unsigned char>(4 + i)));
  }

  auto mid = src.view(8, 4);
  REQUIRE(mid.has_value());
  for (int i = 0; i < 4; ++i) {
    REQUIRE(mid->data[i] ==
            static_cast<std::byte>(static_cast<unsigned char>(8 + i)));
  }

  REQUIRE_FALSE(src.read(13, buf.data(), 4));  // 13+4 > 16
  REQUIRE_FALSE(src.view(16, 1).has_value());
  REQUIRE(src.view(16, 0).has_value());  // empty view exactly at EOF
  REQUIRE(src.read(16, buf.data(), 0));
}

TEST_CASE("MemoryByteSource rejects overflow ranges without wrapping",
          "[io][bytesource]") {
  MemoryByteSource src(make_pattern(5));

  // offset + length would wrap uint64 if added naively.
  REQUIRE_FALSE(src.read(4, nullptr, std::numeric_limits<std::size_t>::max()));
  REQUIRE_FALSE(src.view(4, std::numeric_limits<std::size_t>::max()).has_value());

  // offset far above size.
  REQUIRE_FALSE(src.read(kU64Max - 3, nullptr, 10));
  REQUIRE_FALSE(src.view(kU64Max - 3, 10).has_value());
  REQUIRE_FALSE(src.read(kU64Max, nullptr, 0));  // offset == max > size
}

TEST_CASE("MemoryByteSource requires a valid out buffer for nonzero reads",
          "[io][bytesource]") {
  MemoryByteSource src(make_pattern(4));
  REQUIRE_FALSE(src.read(0, nullptr, 1));
  REQUIRE(src.read(0, nullptr, 0));  // zero-length copy needs no buffer
}

TEST_CASE("MemoryByteSource copies ownership", "[io][bytesource]") {
  auto data = make_pattern(4);
  MemoryByteSource src(data);
  data.clear();  // mutating the original buffer must not affect the source
  std::array<std::byte, 4> buf{};
  REQUIRE(src.read(0, buf.data(), 4));
  REQUIRE(buf[0] == std::byte{0});
  REQUIRE(buf[3] == std::byte{3});
}

TEST_CASE("MappedFileByteSource maps a real file", "[io][bytesource]") {
  const auto path = test_file_path();
  write_file(path, make_pattern(256));

  std::unique_ptr<IByteSource> src;
  const std::error_code ec = open_mapped_file(path, src);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  REQUIRE_FALSE(ec);
  REQUIRE(src != nullptr);
  REQUIRE(src->size() == 256);

  std::array<std::byte, 8> buf{};
  REQUIRE(src->read(100, buf.data(), 8));
  for (int i = 0; i < 8; ++i) {
    REQUIRE(buf[static_cast<std::size_t>(i)] ==
            static_cast<std::byte>(static_cast<unsigned char>(100 + i)));
  }

  auto v = src->view(200, 8);
  REQUIRE(v.has_value());
  for (int i = 0; i < 8; ++i) {
    REQUIRE(v->data[i] ==
            static_cast<std::byte>(static_cast<unsigned char>(200 + i)));
  }

  REQUIRE_FALSE(src->read(254, buf.data(), 8));  // past EOF
  REQUIRE_FALSE(src->view(256, 1).has_value());
  REQUIRE(src->view(256, 0).has_value());  // empty view at EOF
}

TEST_CASE("MappedFileByteSource handles an empty file", "[io][bytesource]") {
  const auto path = test_file_path();
  write_file(path, {});

  std::unique_ptr<IByteSource> src;
  const std::error_code ec = open_mapped_file(path, src);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  REQUIRE_FALSE(ec);
  REQUIRE(src != nullptr);
  REQUIRE(src->size() == 0);
  std::byte out = std::byte{0};
  REQUIRE_FALSE(src->read(0, &out, 1));
  auto v = src->view(0, 0);
  REQUIRE(v.has_value());
  REQUIRE(v->empty());
}

TEST_CASE("open_mapped_file fails cleanly for a missing file", "[io][bytesource]") {
  std::unique_ptr<IByteSource> src;
  const std::error_code ec =
      open_mapped_file(std::filesystem::temp_directory_path() /
                       "pnga_definitely_missing_file.bin", src);
  REQUIRE(static_cast<bool>(ec));
  REQUIRE(src == nullptr);
}

TEST_CASE("Synthetic source reports a 64-bit size above 4 GiB", "[io][bytesource]") {
  constexpr std::uint64_t kHuge = (std::uint64_t{1} << 32) + 0x123;  // 4 GiB + 291
  FakeHugeSource src(kHuge);

  // size() must not be truncated to 32 bits.
  REQUIRE(src.size() == kHuge);
  REQUIRE(src.size() > (std::uint64_t{1} << 32));

  // Requests near or beyond the huge size must fail, never wrap.
  std::byte out{0};
  REQUIRE_FALSE(src.read(0, &out, 1));
  REQUIRE_FALSE(src.read(kHuge, &out, 0));
  REQUIRE_FALSE(src.view(kHuge - 1, 1).has_value());
  // A range that would overflow uint64 is rejected, not wrapped.
  REQUIRE_FALSE(src.read(kU64Max - 0xF, nullptr, 0x20));
}

TEST_CASE("ByteSource implementations are safe for concurrent reads",
          "[io][bytesource]") {
  MemoryByteSource src(make_pattern(1024));
  std::atomic<bool> failed{false};

  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&src, &failed] {
      std::array<std::byte, 64> buf{};
      for (int i = 0; i < 200; ++i) {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(i * 13 % (1024 - 64));
        if (!src.read(offset, buf.data(), 64)) {
          failed.store(true);
          return;
        }
        auto v = src.view(offset, 64);
        if (!v.has_value() || v->data[0] != buf[0]) {
          failed.store(true);
          return;
        }
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  REQUIRE_FALSE(failed.load());
}
