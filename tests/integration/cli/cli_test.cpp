// WP-103 CLI integration tests: run the real pnga binary against generated
// PNG fixtures and compare deterministic JSON output and exit codes.
// WP-602E adds the `statistics` command contract: stdout carries only the
// report bytes, stderr only diagnostics, with the frozen exit-code table.

#include <catch2/catch_test_macros.hpp>

#include <pnga/analysis-engine/statistics_collector.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/validation.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/statistics/serialization.h>

#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
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
                                   std::optional<std::uint32_t> crc_override =
                                       std::nullopt) {
  std::vector<std::byte> data(length, B(0x11));
  if (std::string(type) == "IHDR" && length >= 13) {
    data.assign(length, B(0));
    data[3] = B(1);
    data[7] = B(1);
    data[8] = B(8);
    data[9] = B(6);
  } else if (std::string(type) == "IDAT" && length >= 2) {
    data[0] = B(0x78);
    data[1] = B(0x9c);
  }
  uLong computed = crc32(0L, Z_NULL, 0);
  computed = crc32(computed, reinterpret_cast<const Bytef*>(type), 4);
  if (!data.empty()) {
    computed = crc32(computed, reinterpret_cast<const Bytef*>(data.data()),
                     static_cast<uInt>(data.size()));
  }
  const std::uint32_t crc = crc_override.value_or(
      static_cast<std::uint32_t>(computed));
  std::vector<std::byte> out;
  out.push_back(B(static_cast<unsigned char>(length >> 24)));
  out.push_back(B(static_cast<unsigned char>(length >> 16)));
  out.push_back(B(static_cast<unsigned char>(length >> 8)));
  out.push_back(B(static_cast<unsigned char>(length)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  out.insert(out.end(), data.begin(), data.end());
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
  std::string stderr_text;
};

// Core runner: redirects stdout and stderr of the real binary into two
// temporary files and returns both streams plus the exit code. With
// `normalize_newline` the stdout trailing puts() line ending (LF on POSIX,
// CRLF under Windows text-mode redirection) is dropped so the WP-103 goldens
// stay byte-exact on every platform; the statistics contract tests capture
// exact bytes instead.
CliResult run_cli_streams(const std::string& args, bool normalize_newline) {
  const std::string outfile =
      (std::filesystem::temp_directory_path() / "pnga_cli_stdout.txt").string();
  const std::string errfile =
      (std::filesystem::temp_directory_path() / "pnga_cli_stderr.txt").string();
  const std::string cmd = quote_if_needed(kCliPath) + " " + args + " > " +
                          quote(outfile) + " 2> " + quote(errfile);
  const int rc = std::system(cmd.c_str());
#ifdef _WIN32
  const int exit_code = rc;
#else
  const int exit_code = WEXITSTATUS(rc);
#endif
  const auto read_all = [](const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  };
  std::string content = read_all(outfile);
  if (normalize_newline) {
    if (!content.empty() && content.back() == '\n') {
      content.pop_back();
    }
    if (!content.empty() && content.back() == '\r') {
      content.pop_back();
    }
  }
  return {exit_code, content, read_all(errfile)};
}

// WP-103 inspect/validate runner: stdout only, trailing newline normalized.
CliResult run_cli(const std::string& args) {
  return run_cli_streams(args, /*normalize_newline=*/true);
}

// WP-602E statistics runner: exact bytes on both streams.
CliResult run_cli_exact(const std::string& args) {
  return run_cli_streams(args, /*normalize_newline=*/false);
}

// --- WP-602E statistics fixtures --------------------------------------------

// zlib stream with fixed stored blocks (level 0) so the token count is
// deterministic without a decoder: one literal per filtered byte.
std::vector<std::byte> zlib_deflate_level0(const std::vector<std::byte>& raw) {
  z_stream strm{};
  if (deflateInit2(&strm, 0, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(raw.data()));
  strm.avail_in = static_cast<uInt>(raw.size());
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  if (rc != Z_STREAM_END) {
    return {};
  }
  out.resize(strm.total_out);
  return out;
}

// Builds a valid grayscale 8-bit PNG with the given width/height, one IDAT
// chunk and a level-0 zlib stream (deterministic token counts).
std::vector<std::byte> gray_level0_png(std::uint32_t w, std::uint32_t h) {
  std::vector<std::byte> filtered;
  for (std::uint32_t y = 0; y < h; ++y) {
    filtered.push_back(std::byte{0});  // filter type None
    for (std::uint32_t x = 0; x < w; ++x) {
      filtered.push_back(static_cast<std::byte>(1 + ((x * 3 + y * 7) % 250)));
    }
  }
  std::vector<std::byte> bytes(pnga::png_format::kPngSignature.begin(),
                               pnga::png_format::kPngSignature.end());
  const auto push = [&bytes](const char* type,
                             const std::vector<std::byte>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(std::byte(static_cast<unsigned char>((len >> shift) & 0xFFu)));
    }
    uLong crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(std::byte(static_cast<unsigned char>(type[i])));
    }
    if (!data.empty()) {
      crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uInt>(data.size()));
      bytes.insert(bytes.end(), data.begin(), data.end());
    }
    for (const int shift : {24, 16, 8, 0}) {
      bytes.push_back(std::byte(static_cast<unsigned char>((crc >> shift) & 0xFFu)));
    }
  };
  std::vector<std::byte> ihdr(13, std::byte{0});
  ihdr[0] = std::byte(static_cast<unsigned char>((w >> 24) & 0xFFu));
  ihdr[1] = std::byte(static_cast<unsigned char>((w >> 16) & 0xFFu));
  ihdr[2] = std::byte(static_cast<unsigned char>((w >> 8) & 0xFFu));
  ihdr[3] = std::byte(static_cast<unsigned char>(w & 0xFFu));
  ihdr[4] = std::byte(static_cast<unsigned char>((h >> 24) & 0xFFu));
  ihdr[5] = std::byte(static_cast<unsigned char>((h >> 16) & 0xFFu));
  ihdr[6] = std::byte(static_cast<unsigned char>((h >> 8) & 0xFFu));
  ihdr[7] = std::byte(static_cast<unsigned char>(h & 0xFFu));
  ihdr[8] = std::byte{8};  // bit depth
  ihdr[9] = std::byte{0};  // color type gray
  push("IHDR", ihdr);
  push("IDAT", zlib_deflate_level0(filtered));
  push("IEND", {});
  return bytes;
}

// Mirrors the CLI's synchronous composition with the same public APIs and
// request values so the direct serializer output is the exact expected report.
std::string direct_statistics_bytes(const std::vector<std::byte>& png,
                                    bool csv) {
  using pnga::analysis_engine::StatisticsCollectionRequest;
  auto source = std::make_shared<pnga::io::MemoryByteSource>(png);
  const auto chunks = pnga::png_format::index_chunks(*source);
  const auto stages = std::make_shared<const pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source));
  StatisticsCollectionRequest request;
  request.generation = 0;
  request.source = source;
  request.chunks = chunks;
  request.stages = stages;
  request.limits = pnga::statistics::StatisticsLimits{};
  request.max_working_bytes = 64ull << 20;
  const auto result =
      pnga::analysis_engine::collect_document_statistics(request, nullptr, {});
  const pnga::statistics::SerializationResult serialized =
      csv ? pnga::statistics::serialize_statistics_csv(result.document,
                                                       result.snapshot)
          : pnga::statistics::serialize_statistics_json(result.document,
                                                        result.snapshot);
  REQUIRE(serialized.success);
  return serialized.bytes;
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
  write_file(path, png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                              chunk_bytes("IEND", 0)}));

  const CliResult r = run_cli("validate " + quote(path.string()) + " --json");
  const std::string expected = std::string("{\"file\":\"") +
                               json_escape(path.string()) +
                               "\",\"size\":65,\"valid\":true,\"issues\":[]}";
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.stdout_text == expected);
}

TEST_CASE("pnga validate --json reports trailing bytes after IEND",
          "[cli][wp103]") {
  auto data = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                         chunk_bytes("IEND", 0)});
  data.insert(data.end(), {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}});
  const auto path = test_file("pnga_trailing.png");
  write_file(path, data);

  const CliResult r = run_cli("validate " + quote(path.string()) + " --json");
  const std::string expected =
      std::string("{\"file\":\"") + json_escape(path.string()) +
      "\",\"size\":68,\"valid\":false,"
      "\"issues\":[{\"rule_id\":\"data_after_iend\",\"severity\":\"error\","
      "\"message\":\"bytes appear after the IEND chunk\",\"offset\":65,"
      "\"spec_ref\":\"PNG:5.2\"}]}";
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

TEST_CASE("pnga statistics --help documents the schema and exit codes",
          "[cli][wp602e]") {
  const CliResult r = run_cli_exact("statistics --help");
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.stderr_text.empty());
  REQUIRE(r.stdout_text.find("pnga.statistics schema_version=1") !=
          std::string::npos);
  REQUIRE(r.stdout_text.find("0 ready") != std::string::npos);
  REQUIRE(r.stdout_text.find("1 I/O") != std::string::npos);
  REQUIRE(r.stdout_text.find("2 argument") != std::string::npos);
  REQUIRE(r.stdout_text.find("3 validation") != std::string::npos);
  REQUIRE(r.stdout_text.find("4 partial") != std::string::npos);
}

TEST_CASE("pnga statistics emits ready JSON report bytes on stdout only",
          "[cli][wp602e]") {
  const auto png = gray_level0_png(24, 8);
  const auto path = test_file("pnga_statistics_ready.png");
  write_file(path, png);

  const CliResult r =
      run_cli_exact("statistics " + quote(path.string()) + " --format json");
  REQUIRE(r.exit_code == 0);
  // stdout carries only report bytes, byte-identical to the shared serializer.
  REQUIRE(r.stdout_text == direct_statistics_bytes(png, /*csv=*/false));
  REQUIRE(r.stdout_text.find("pnga:") == std::string::npos);
  // stderr carries only diagnostics and stays empty on success.
  REQUIRE(r.stderr_text.empty());
}

TEST_CASE("pnga statistics emits ready CSV report bytes on stdout only",
          "[cli][wp602e]") {
  const auto png = gray_level0_png(24, 8);
  const auto path = test_file("pnga_statistics_ready.png");
  write_file(path, png);

  const CliResult r =
      run_cli_exact("statistics " + quote(path.string()) + " --format csv");
  REQUIRE(r.exit_code == 0);
  REQUIRE(r.stdout_text == direct_statistics_bytes(png, /*csv=*/true));
  REQUIRE(r.stdout_text.rfind("schema_version,section,metric,key,value,unit\n",
                              0) == 0);
  REQUIRE(r.stderr_text.empty());
}

TEST_CASE("pnga statistics reports a missing file with exit code 1",
          "[cli][wp602e]") {
  const auto path = test_file("pnga_statistics_missing.png");
  const CliResult r =
      run_cli_exact("statistics " + quote(path.string()) + " --format json");
  REQUIRE(r.exit_code == 1);
  REQUIRE(r.stdout_text.empty());
  REQUIRE_FALSE(r.stderr_text.empty());
}

TEST_CASE("pnga statistics rejects argument and format errors with exit 2",
          "[cli][wp602e]") {
  const auto path = test_file("pnga_statistics_args.png");
  write_file(path, gray_level0_png(4, 4));

  const auto run = [&path](const std::string& args) {
    return run_cli_exact("statistics " + args);
  };
  // Missing --format.
  REQUIRE(run(quote(path.string())).exit_code == 2);
  // Missing --format value.
  REQUIRE(run(quote(path.string()) + " --format").exit_code == 2);
  // Empty --format= value.
  REQUIRE(run(quote(path.string()) + " --format=").exit_code == 2);
  // Unknown --format value.
  REQUIRE(run(quote(path.string()) + " --format xml").exit_code == 2);
  // Duplicate --format.
  REQUIRE(run(quote(path.string()) + " --format json --format csv")
              .exit_code == 2);
  // Missing file argument.
  REQUIRE(run("--format json").exit_code == 2);
  // More than one file argument.
  REQUIRE(run(quote(path.string()) + " " + quote(path.string()) +
              " --format json")
              .exit_code == 2);
  // Unknown option.
  REQUIRE(run(quote(path.string()) + " --json --format json").exit_code == 2);
  // stdout stays empty; diagnostics go to stderr only.
  const CliResult r = run(quote(path.string()) + " --format xml");
  REQUIRE(r.exit_code == 2);
  REQUIRE(r.stdout_text.empty());
  REQUIRE_FALSE(r.stderr_text.empty());
}

TEST_CASE("pnga statistics reports malformed input with usable statistics "
          "as exit 3",
          "[cli][wp602e]") {
  // Truncate the file so the IDAT chunk is incomplete: the chunk envelope is
  // structurally invalid (a validation issue) while the collected Chunk
  // statistics remain a usable verified prefix.
  auto png = gray_level0_png(24, 8);
  png.resize(png.size() - 20);  // cuts into the IDAT data, CRC and IEND
  const auto path = test_file("pnga_statistics_truncated.png");
  write_file(path, png);

  const CliResult r =
      run_cli_exact("statistics " + quote(path.string()) + " --format json");
  REQUIRE(r.exit_code == 3);
  REQUIRE(r.stdout_text == direct_statistics_bytes(png, /*csv=*/false));
  REQUIRE(r.stderr_text.empty());
}

TEST_CASE("pnga statistics reports budget-limited statistics as exit 4",
          "[cli][wp602e]") {
  // A level-0 stored stream wider than the frozen 1,048,576 token sample
  // budget: tokens/lengths/distances stop budget_exceeded while the other
  // sections stay ready, and the file is structurally valid.
  const auto png = gray_level0_png(1'048'600, 1);
  const auto path = test_file("pnga_statistics_budget.png");
  write_file(path, png);

  const CliResult r =
      run_cli_exact("statistics " + quote(path.string()) + " --format json");
  REQUIRE(r.exit_code == 4);
  REQUIRE(r.stdout_text == direct_statistics_bytes(png, /*csv=*/false));
  REQUIRE(r.stderr_text.empty());
}
