// WP-APNG-INSPECT T05: shared section emitters extracted verbatim from
// serialization.cpp so the frame serializer reuses the exact section
// encoding without duplicating it. Changing this file changes the v1
// goldens; the static goldens must stay byte-identical.

#include "serialization_sections.h"

namespace pnga::statistics::serialization_sections_internal {

void append_json_sections(std::string& out, const StatisticsSnapshot& snapshot) {
  const auto numeric = [](std::uint64_t value, bool available) {
    std::string text;
    if (available) {
      append_u64(text, value);
    } else {
      text = "null";
    }
    return text;
  };
  const auto bool_text = [](bool value) { return value ? "true" : "false"; };

  {
    const bool section_available =
        begin_json_section(out, "    ", "overview", snapshot.overview.state);
    // The compression totals are a pair with one availability flag: both
    // values serialize only when the section carries evidence AND the
    // totals were fully verified. A half-verified pair stays null/empty,
    // never a zero value.
    const bool totals_available =
        section_available && snapshot.overview.data.has_compression_totals;
    out += "      \"compressed_bytes\": ";
    out += numeric(snapshot.overview.data.compressed_bytes, totals_available);
    out += ",\n";
    out += "      \"inflated_bytes\": ";
    out += numeric(snapshot.overview.data.inflated_bytes, totals_available);
    out += ",\n";
    out += "      \"has_compression_totals\": ";
    out += bool_text(snapshot.overview.data.has_compression_totals);
    out += "\n    }";
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "chunks", snapshot.chunks.state);
    out += "      \"count\": ";
    out += numeric(snapshot.chunks.data.count, available);
    out += ",\n";
    out += "      \"data_bytes\": ";
    out += numeric(snapshot.chunks.data.data_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.chunks.data.buckets.size(); ++i) {
        const ChunkBucket& bucket = snapshot.chunks.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"type\": ";
        append_json_string(out, bucket.type);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += ", \"data_bytes\": ";
        append_u64(out, bucket.data_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.chunks.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "filters", snapshot.filters.state);
    out += "      \"rows\": ";
    out += numeric(snapshot.filters.data.rows, available);
    out += ",\n";
    out += "      \"data_bytes\": ";
    out += numeric(snapshot.filters.data.data_bytes, available);
    out += ",\n";
    out += "      \"invalid_rows\": ";
    out += numeric(snapshot.filters.data.invalid_rows, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.filters.data.buckets.size(); ++i) {
        const FilterBucket& bucket = snapshot.filters.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"rows\": ";
        append_u64(out, bucket.rows);
        out += ", \"data_bytes\": ";
        append_u64(out, bucket.data_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.filters.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "blocks", snapshot.blocks.state);
    out += "      \"count\": ";
    out += numeric(snapshot.blocks.data.count, available);
    out += ",\n";
    out += "      \"compressed_bits\": ";
    out += numeric(snapshot.blocks.data.compressed_bits, available);
    out += ",\n";
    out += "      \"output_bytes\": ";
    out += numeric(snapshot.blocks.data.output_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.blocks.data.buckets.size(); ++i) {
        const BlockBucket& bucket = snapshot.blocks.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"blocks\": ";
        append_u64(out, bucket.blocks);
        out += ", \"compressed_bits\": ";
        append_u64(out, bucket.compressed_bits);
        out += ", \"output_bytes\": ";
        append_u64(out, bucket.output_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.blocks.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "tokens", snapshot.tokens.state);
    out += "      \"count\": ";
    out += numeric(snapshot.tokens.data.count, available);
    out += ",\n";
    out += "      \"input_bits\": ";
    out += numeric(snapshot.tokens.data.input_bits, available);
    out += ",\n";
    out += "      \"output_bytes\": ";
    out += numeric(snapshot.tokens.data.output_bytes, available);
    out += ",\n";
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.tokens.data.buckets.size(); ++i) {
        const TokenBucket& bucket = snapshot.tokens.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"count\": ";
        append_u64(out, bucket.count);
        out += ", \"input_bits\": ";
        append_u64(out, bucket.input_bits);
        out += ", \"output_bytes\": ";
        append_u64(out, bucket.output_bytes);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.tokens.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "lengths", snapshot.lengths.state);
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.lengths.data.buckets.size(); ++i) {
        const ValueBucket& bucket = snapshot.lengths.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"value\": ";
        append_u64(out, bucket.value);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.lengths.data.buckets.size()
                                             : 0);
  }
  {
    out += ",\n";
    const bool available =
        begin_json_section(out, "    ", "distances", snapshot.distances.state);
    append_json_bucket_array_open(out, "    ");
    if (available) {
      for (std::size_t i = 0; i < snapshot.distances.data.buckets.size(); ++i) {
        const ValueBucket& bucket = snapshot.distances.data.buckets[i];
        out += i == 0 ? "\n        " : ",\n        ";
        out += "{\"value\": ";
        append_u64(out, bucket.value);
        out += ", \"count\": ";
        append_u64(out, bucket.count);
        out += '}';
      }
    }
    append_json_bucket_array_close(out, "    ",
                                   available ? snapshot.distances.data.buckets.size()
                                             : 0);
  }
}

void append_csv_sections(std::string& out, std::string& scratch,
                         std::string_view leading_cells,
                         const StatisticsSnapshot& snapshot) {
  const auto row = [&](std::string_view section, std::string_view metric,
                       std::string_view key, std::string_view value,
                       std::string_view unit) {
    append_csv_row_cells(out, leading_cells, section, metric, key, value, unit);
  };
  const auto numeric_row = [&](std::string_view section,
                               std::string_view metric, std::string_view key,
                               std::uint64_t value, bool available,
                               std::string_view unit) {
    if (!available) {
      append_csv_row_cells(out, leading_cells, section, metric, key, "", unit);
      return;
    }
    append_csv_u64_value(scratch, out, leading_cells, section, metric, key,
                         value, unit);
  };

  const auto section_header = [&](std::string_view name,
                                  const SectionState& state) {
    row(name, "status", "", status_text(state.status), "");
    row(name, "complete", "", state.complete ? "true" : "false", "");
    row(name, "scope", "", scope_text(state.scope), "");
    if (!state.error.empty()) {
      row(name, "error", "", state.error, "");
    }
    return state.status != SectionStatus::kUnavailable;
  };

  {
    const bool section_available =
        section_header("overview", snapshot.overview.state);
    // Same pair semantics as the JSON side: both numeric rows carry values
    // only when the totals were fully verified.
    const bool totals_available =
        section_available && snapshot.overview.data.has_compression_totals;
    numeric_row("overview", "compressed_bytes", "",
                snapshot.overview.data.compressed_bytes, totals_available,
                "bytes");
    numeric_row("overview", "inflated_bytes", "",
                snapshot.overview.data.inflated_bytes, totals_available,
                "bytes");
    row("overview", "has_compression_totals", "",
        snapshot.overview.data.has_compression_totals ? "true" : "false", "");
  }
  {
    const bool available = section_header("chunks", snapshot.chunks.state);
    numeric_row("chunks", "count", "", snapshot.chunks.data.count, available,
                "chunks");
    numeric_row("chunks", "data_bytes", "", snapshot.chunks.data.data_bytes,
                available, "bytes");
    if (available) {
      for (const ChunkBucket& bucket : snapshot.chunks.data.buckets) {
        append_csv_u64_value(scratch, out, leading_cells, "chunks", "bucket.count", bucket.type,
                             bucket.count, "chunks");
        append_csv_u64_value(scratch, out, leading_cells, "chunks", "bucket.data_bytes",
                             bucket.type, bucket.data_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("filters", snapshot.filters.state);
    numeric_row("filters", "rows", "", snapshot.filters.data.rows, available,
                "rows");
    numeric_row("filters", "data_bytes", "", snapshot.filters.data.data_bytes,
                available, "bytes");
    numeric_row("filters", "invalid_rows", "",
                snapshot.filters.data.invalid_rows, available, "rows");
    if (available) {
      for (std::size_t i = 0; i < snapshot.filters.data.buckets.size(); ++i) {
        const FilterBucket& bucket = snapshot.filters.data.buckets[i];
        scratch.clear();
        append_u64(scratch, i);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, leading_cells, "filters", "bucket.rows", key,
                             bucket.rows, "rows");
        append_csv_u64_value(scratch, out, leading_cells, "filters", "bucket.data_bytes", key,
                             bucket.data_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("blocks", snapshot.blocks.state);
    numeric_row("blocks", "count", "", snapshot.blocks.data.count, available,
                "blocks");
    numeric_row("blocks", "compressed_bits", "",
                snapshot.blocks.data.compressed_bits, available, "bits");
    numeric_row("blocks", "output_bytes", "",
                snapshot.blocks.data.output_bytes, available, "bytes");
    if (available) {
      static constexpr std::string_view kBlockKeys[] = {"stored", "fixed",
                                                        "dynamic"};
      for (std::size_t i = 0; i < snapshot.blocks.data.buckets.size() &&
                               i < std::size(kBlockKeys);
           ++i) {
        const BlockBucket& bucket = snapshot.blocks.data.buckets[i];
        append_csv_u64_value(scratch, out, leading_cells, "blocks", "bucket.blocks",
                             kBlockKeys[i], bucket.blocks, "blocks");
        append_csv_u64_value(scratch, out, leading_cells, "blocks", "bucket.compressed_bits",
                             kBlockKeys[i], bucket.compressed_bits, "bits");
        append_csv_u64_value(scratch, out, leading_cells, "blocks", "bucket.output_bytes",
                             kBlockKeys[i], bucket.output_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("tokens", snapshot.tokens.state);
    numeric_row("tokens", "count", "", snapshot.tokens.data.count, available,
                "tokens");
    numeric_row("tokens", "input_bits", "", snapshot.tokens.data.input_bits,
                available, "bits");
    numeric_row("tokens", "output_bytes", "",
                snapshot.tokens.data.output_bytes, available, "bytes");
    if (available) {
      static constexpr std::string_view kTokenKeys[] = {"literal", "match",
                                                        "eob"};
      for (std::size_t i = 0; i < snapshot.tokens.data.buckets.size() &&
                               i < std::size(kTokenKeys);
           ++i) {
        const TokenBucket& bucket = snapshot.tokens.data.buckets[i];
        append_csv_u64_value(scratch, out, leading_cells, "tokens", "bucket.count",
                             kTokenKeys[i], bucket.count, "tokens");
        append_csv_u64_value(scratch, out, leading_cells, "tokens", "bucket.input_bits",
                             kTokenKeys[i], bucket.input_bits, "bits");
        append_csv_u64_value(scratch, out, leading_cells, "tokens", "bucket.output_bytes",
                             kTokenKeys[i], bucket.output_bytes, "bytes");
      }
    }
  }
  {
    const bool available = section_header("lengths", snapshot.lengths.state);
    if (available) {
      for (const ValueBucket& bucket : snapshot.lengths.data.buckets) {
        scratch.clear();
        append_u64(scratch, bucket.value);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, leading_cells, "lengths", "bucket.count", key,
                             bucket.count, "matches");
      }
    }
  }
  {
    const bool available = section_header("distances", snapshot.distances.state);
    if (available) {
      for (const ValueBucket& bucket : snapshot.distances.data.buckets) {
        scratch.clear();
        append_u64(scratch, bucket.value);
        const std::string key = scratch;
        append_csv_u64_value(scratch, out, leading_cells, "distances", "bucket.count", key,
                             bucket.count, "matches");
      }
    }
  }
}

}  // namespace pnga::statistics::serialization_sections_internal
