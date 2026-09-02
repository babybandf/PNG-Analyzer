#include <pnga/trace-model/compression_navigation.h>

#include <cstddef>

namespace pnga::trace_model {

bool DocumentSourceUnit::valid() const noexcept {
  if (kind == DocumentSourceUnitKind::kFile) {
    return index == 0;  // a static document has exactly one file unit.
  }
  return true;  // animation frames are identified by a free-running index.
}

bool CompressionNavigationTarget::valid() const noexcept {
  if (!source_unit.valid() || request_serial == 0) {
    return false;  // emitted serials are non-zero.
  }
  const bool logical_ok = std::visit(
      [](const auto& range) { return range.valid() && !range.empty(); },
      logical_range);
  if (!logical_ok) {
    return false;  // every logical range is non-empty.
  }
  for (const FileByteRange& span : physical_spans) {
    if (!span.valid() || span.empty()) {
      return false;
    }
  }
  for (std::size_t i = 1; i < physical_spans.size(); ++i) {
    if (physical_spans[i - 1].end > physical_spans[i].begin) {
      return false;  // out of order or overlapping in caller order.
    }
  }
  const bool inflated = std::holds_alternative<InflatedByteRange>(logical_range);
  if (inflated) {
    if (!physical_spans.empty()) {
      return false;  // Inflated ranges require no physical spans.
    }
  } else if (!std::holds_alternative<FileByteRange>(logical_range) &&
             physical_spans.empty()) {
    return false;  // compressed inputs require one non-empty physical span.
  }
  return true;
}

}  // namespace pnga::trace_model
