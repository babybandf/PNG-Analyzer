// WP-602E: the `statistics` subcommand. Strict argument parsing, synchronous
// whole-document collection through the shared collector and serialization
// through the sole shared serializer. The command owns only composition: no
// schema field, escaping or ordering rule lives here. stdout receives the
// report bytes with fwrite (never puts: the serializer already terminates the
// report with exactly one LF); stderr receives only diagnostics.

#include "statistics_command.h"

#include <pnga/analysis-engine/statistics_collector.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/validation.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/statistics/serialization.h>
#include <pnga/statistics/statistics.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace pnga::cli {
namespace {

// Frozen statistics exit codes (WP-602E ruling R10). Distinct from the WP-103
// codes only in meaning; the numeric values 0-3 are shared by contract.
constexpr int kExitReady = 0;
constexpr int kExitIoError = 1;
constexpr int kExitArgumentError = 2;
constexpr int kExitValidationIssues = 3;
constexpr int kExitStatisticsPartial = 4;

// Declared working memory of the synchronous CLI collection: the frozen
// background cap (the collector rejects anything larger).
constexpr std::uint64_t kMaxWorkingBytes = 64ull << 20;

}  // namespace

void print_statistics_usage(FILE* out) {
  std::fprintf(out,
               "pnga statistics - whole-document statistics report\n"
               "schema: pnga.statistics schema_version=1\n"
               "usage:\n"
               "  pnga statistics <file> --format json|csv\n"
               "output:\n"
               "  stdout carries only the report bytes (UTF-8, LF, exactly\n"
               "  one trailing newline); stderr carries only diagnostics.\n"
               "exit codes:\n"
               "  0 ready\n"
               "  1 I/O failure (the file could not be read)\n"
               "  2 argument or format error (missing/duplicate/unknown\n"
               "    --format, missing or duplicate file argument)\n"
               "  3 validation issues with usable statistics\n"
               "  4 partial, cancelled or budget-limited statistics\n");
}

namespace {

struct StatisticsArguments {
  bool help = false;
  std::filesystem::path file;
  StatisticsOutputFormat format = StatisticsOutputFormat::kJson;
  bool has_format = false;
};

// Strict parsing: exactly one positional file and exactly one --format value.
// Any unknown option, duplicate or missing piece is an argument error.
std::optional<StatisticsArguments> parse_statistics_arguments(
    int first, int argc, char** argv, FILE* err) {
  StatisticsArguments parsed;
  bool has_file = false;
  for (int i = first; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      parsed.help = true;
      continue;
    }
    if (arg.rfind("--format", 0) == 0 &&
        (arg.size() == 8 || arg[8] == '=')) {
      std::string_view value;
      if (arg.size() == 9 && arg[8] == '=') {
        std::fprintf(err,
                     "pnga statistics: --format requires a value "
                     "(json or csv)\n");
        return std::nullopt;
      }
      if (arg.size() > 9 && arg[8] == '=') {
        value = arg.substr(9);
      } else if (arg.size() == 8) {
        if (i + 1 < argc) {
          value = argv[++i];
        } else {
          std::fprintf(err,
                       "pnga statistics: --format requires a value "
                       "(json or csv)\n");
          return std::nullopt;
        }
      }
      if (parsed.has_format) {
        std::fprintf(err, "pnga statistics: --format given more than once\n");
        return std::nullopt;
      }
      if (value == "json") {
        parsed.format = StatisticsOutputFormat::kJson;
      } else if (value == "csv") {
        parsed.format = StatisticsOutputFormat::kCsv;
      } else {
        std::fprintf(err,
                     "pnga statistics: unknown --format value '%.*s' "
                     "(json or csv)\n",
                     static_cast<int>(value.size()), value.data());
        return std::nullopt;
      }
      parsed.has_format = true;
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      std::fprintf(err, "pnga statistics: unknown option '%.*s'\n",
                   static_cast<int>(arg.size()), arg.data());
      return std::nullopt;
    }
    if (has_file) {
      std::fprintf(err,
                   "pnga statistics: exactly one file argument is required\n");
      return std::nullopt;
    }
    parsed.file = std::filesystem::path(std::string(arg));
    has_file = true;
  }
  if (parsed.help) {
    return parsed;
  }
  if (!has_file) {
    std::fprintf(err, "pnga statistics: missing file argument\n");
    return std::nullopt;
  }
  if (!parsed.has_format) {
    std::fprintf(err, "pnga statistics: missing --format (json or csv)\n");
    return std::nullopt;
  }
  return parsed;
}

// A section is usable when it carries collected evidence a consumer can act
// on; unavailable, cancelled and error sections carry none.
bool section_usable(const pnga::statistics::SectionState& state) noexcept {
  switch (state.status) {
    case pnga::statistics::SectionStatus::kUnavailable:
    case pnga::statistics::SectionStatus::kCancelled:
    case pnga::statistics::SectionStatus::kError:
      return false;
    default:
      return true;
  }
}

bool any_section_usable(
    const pnga::statistics::StatisticsSnapshot& snapshot) noexcept {
  const auto& s = snapshot;
  return section_usable(s.overview.state) || section_usable(s.chunks.state) ||
         section_usable(s.filters.state) || section_usable(s.blocks.state) ||
         section_usable(s.tokens.state) || section_usable(s.lengths.state) ||
         section_usable(s.distances.state);
}

}  // namespace

int run_statistics_command_line(int argc, char** argv, FILE* out, FILE* err) {
  const auto parsed = parse_statistics_arguments(/*first=*/2, argc, argv, err);
  if (!parsed.has_value()) {
    print_statistics_usage(err);
    return kExitArgumentError;
  }
  if (parsed->help) {
    print_statistics_usage(out);
    return kExitReady;
  }
  return run_statistics_command(parsed->file, parsed->format, out, err);
}

int run_statistics_command(const std::filesystem::path& file,
                           StatisticsOutputFormat format,
                           FILE* out, FILE* err) {
#ifdef _WIN32
  _setmode(_fileno(out), _O_BINARY);
#endif
  std::unique_ptr<pnga::io::IByteSource> mapped;
  if (std::error_code ec = pnga::io::open_mapped_file(file, mapped)) {
    std::fprintf(err, "pnga statistics: cannot read '%s' (system error %d)\n",
                 file.string().c_str(), ec.value());
    return kExitIoError;
  }
  std::shared_ptr<const pnga::io::IByteSource> source(std::move(mapped));

  const auto chunks = pnga::png_format::index_chunks(*source);
  const auto validation =
      pnga::analysis_engine::validate_document(*source, chunks);
  const bool has_issues = !validation.issues.empty();

  pnga::analysis_engine::StatisticsCollectionRequest request;
  request.generation = 0;
  request.source = source;
  request.chunks = chunks;
  request.stages = std::make_shared<const pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source));
  request.limits = pnga::statistics::StatisticsLimits{};
  request.max_working_bytes = kMaxWorkingBytes;
  const pnga::analysis_engine::StatisticsCollectionResult result =
      pnga::analysis_engine::collect_document_statistics(request, nullptr, {});

  const pnga::statistics::SerializationResult serialized =
      format == StatisticsOutputFormat::kJson
          ? pnga::statistics::serialize_statistics_json(result.document,
                                                        result.snapshot)
          : pnga::statistics::serialize_statistics_csv(result.document,
                                                       result.snapshot);
  if (!serialized.success) {
    std::fprintf(err, "pnga statistics: serialization failed: %s\n",
                 serialized.error.c_str());
    return kExitStatisticsPartial;
  }
  if (!serialized.bytes.empty() &&
      std::fwrite(serialized.bytes.data(), 1, serialized.bytes.size(), out) !=
          serialized.bytes.size()) {
    std::fprintf(err, "pnga statistics: cannot write the report\n");
    return kExitIoError;
  }

  // Exit mapping: 0 only when every section is ready/complete; 3 when
  // validation found issues and at least one section is usable; 4 for the
  // verified partial/cancel/budget remainder.
  if (result.snapshot.complete()) {
    return has_issues ? kExitValidationIssues : kExitReady;
  }
  if (has_issues && any_section_usable(result.snapshot)) {
    return kExitValidationIssues;
  }
  return kExitStatisticsPartial;
}

}  // namespace pnga::cli
