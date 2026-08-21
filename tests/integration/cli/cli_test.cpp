// WP-103 CLI integration tests: run the real pnga binary against generated
// PNG fixtures and compare deterministic JSON output and exit codes.

#include <catch2/catch_test_macros.hpp>

#include <pnga/png-format/chunk_index.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef PNGA_CLI_PATH
#error "PNGA_CLI_PATH must be defined by the build"
#endif

namespace {

std::string kCliPath = PNGA_CLI_PATH;

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> chunk_bytes(const char* type, std::uint32_t length,
                                   std::uint32_t crc = 0) {
  std::vector<std::byte> out;
  out.push_back(B(static_cast<unsigned char>(length >> 24)));
  out.push_back(B(static_cast<unsigned char>(length >> 16)));
  out.push_back(B(static_cast<unsigned char>(length >> 8)));
  out.push_back(B(static_cast<unsigned char>(length)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  for (std::uint32_t i = 0; i < length; ++i) {
    out.push_back(B(0x11));
  }
  out.push_back(B(static_cast<unsigned char>(crc >> 24)));
  out.push_back(B(static_cast<unsigned char>(crc >> 16)));
  out.push_back(B(static_cast<unsigned char>(crc >> 8)));
  out.push_back(B(static_cast<unsigned char>(crc)));
  return out;
}

std::vector<std::byte> png_bytes(std::vector<std::vector<std::byte>> chunks) {
  std::vector<std::byte> out;
  out.assign(pnga::png_format::kPngSignature.begin(),
             pnga::png_format::kPngSignature.end());
  for (auto& c : chunks) {
    out.insert(out.end(), c.begin(), c.end());
  }
  return out;
}

std::filesystem::path test_file(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

void write_file(const std::filesystem::path& path,
                const std::vector<std::byte>& data) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  os.write(reinterpret_cast<const char*>(data.data()),
           static_cast<std::streamsize>(data.size()));
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

// Windows cmd /c strips the outermost quotes when a command starts with a
// quoted token, which breaks the command and forces errorlevel 1. Only quote
// tokens that actually need it (contain a space) so exit codes propagate.
std::string quote_if_needed(const std::string& s) {
  return s.find(' ') == std::string::npos ? s : quote(s);
}

// Mirrors the CLI's JSON escaping so expected strings stay exact on every
// platform (Windows temp paths contain backslashes that JSON must escape).
std::string json_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

struct CliResult {
  int exit_code;
  std::string stdout_text;
};

CliResult run_cli(const std::string& args) {
  const std::string outfile =
      (std::filesystem::temp_directory_path() / "pnga_cli_stdout.txt").string();
  const std::string cmd = quote_if_needed(kCliPath) + " " + args + " > " +
                          quote(outfile) + " 2>&1";
  const int rc = std::system(cmd.c_str());
#ifdef _WIN32
  const int exit_code = rc;
#else
  const int exit_code = WEXITSTATUS(rc);
#endif
  std::ifstream in(outfile, std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  // Drop a single trailing newline emitted by puts() so goldens are exact.
  if (!content.empty() && content.back() == '\n') {
    content.pop_back();
  }
  return {exit_code, content};
}

}  // namespace

TEST_CASE("pnga inspect --json emits a deterministic chunk tree",
          "[cli][wp103]") {
  const auto path = test_file("pnga_valid.png");
  write_file(path, png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                              chunk_bytes("IEND", 0)}));

  const CliResult r = run_cli("inspect " + quote(path.string()) + " --json");
  const std::string expected =
      std::string("{\"file\":\"") + json_escape(path.string()) +
      "\",\"size\":65,\"signature_valid\":true,\"chunks\":["
      "{\"type\":\"IHDR\",\"header_offset\":8,\"data_offset\":16,"
      "\"data_length\":13,\"crc_offset\":29},"
      "{\"type\":\"IDAT\",\"header_offset\":33,\"data_offset\":41,"
      "\"data_length\":8,\"crc_offset\":49},"
      "{\"type\":\"IEND\",\"header_offset\":53,\"data_offset\":61,"
      "\"data_length\":0,\"crc_offset\":61}],\"issues\":[]}";

  REQUIRE(r.exit_code == 0);
  REQUIRE(r.stdout_text == expected);
}

TEST_CASE("pnga validate --json reports a clean file as valid",
          "[cli][wp103]") {
  const auto path = test_file("pnga_valid.png");
  write_file(path, png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IEND", 0)}));

  const CliResult r = run_cli("validate " + quote(path.string()) + " --json");
  const std::string expected = std::string("{\"file\":\"") +
                               json_escape(path.string()) +
                               "\",\"size\":45,\"valid\":true,\"issues\":[]}";
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.stdout_text == expected);
}

TEST_CASE("pnga validate --json reports trailing bytes after IEND",
          "[cli][wp103]") {
  auto data = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IEND", 0)});
  data.insert(data.end(), {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}});
  const auto path = test_file("pnga_trailing.png");
  write_file(path, data);

  const CliResult r = run_cli("validate " + quote(path.string()) + " --json");
  const std::string expected =
      std::string("{\"file\":\"") + json_escape(path.string()) +
      "\",\"size\":48,\"valid\":false,"
      "\"issues\":[{\"kind\":\"trailing_bytes_after_iend\",\"offset\":45}]}";
  REQUIRE(r.exit_code == 3);  // validation issue
  REQUIRE(r.stdout_text == expected);
}

TEST_CASE("pnga inspect --json reports a truncated header as format error",
          "[cli][wp103]") {
  auto data = png_bytes({});
  data.push_back(std::byte{0x00});  // 1 stray byte after the signature
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});
  const auto path = test_file("pnga_truncated_header.png");
  write_file(path, data);

  const CliResult r = run_cli("inspect " + quote(path.string()) + " --json");
  const std::string expected =
      std::string("{\"file\":\"") + json_escape(path.string()) +
      "\",\"size\":11,\"signature_valid\":true,\"chunks\":[],"
      "\"issues\":[{\"kind\":\"truncated_header\",\"offset\":8}]}";
  REQUIRE(r.exit_code == 2);  // format error
  REQUIRE(r.stdout_text == expected);
}

TEST_CASE("pnga inspect --json reports a missing file with exit code 1",
          "[cli][wp103]") {
  const auto path = test_file("pnga_does_not_exist.png");
  const CliResult r = run_cli("inspect " + quote(path.string()) + " --json");
  REQUIRE(r.exit_code == 1);
  REQUIRE(r.stdout_text.find("\"error\":true") != std::string::npos);
  REQUIRE(r.stdout_text.find("{\"file\":\"" + json_escape(path.string())) == 0);
}

TEST_CASE("pnga rejects an unknown command with the format-error exit code",
          "[cli][wp103]") {
  const CliResult r = run_cli("frobnicate --json");
  REQUIRE(r.exit_code == 2);
}
