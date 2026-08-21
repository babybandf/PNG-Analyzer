#ifndef PNGA_TRACE_MODEL_PROVENANCE_H
#define PNGA_TRACE_MODEL_PROVENANCE_H

// WP-504: backend-neutral reversible coordinate ranges. A span identifies a
// byte range in one analysis space; bit-aligned spans additionally preserve
// the exact bit window within the covered bytes. The model deliberately
// permits several spans for one result because PNG filters and Deflate
// matches are many-to-one and one-to-many relationships.

#include <cstdint>

namespace pnga::trace_model {

enum class ProvenanceSpace {
  kNativeSample,
  kReconstructed,
  kFiltered,
  kInflatedOutput,
  kLogicalDeflate,
  kPhysicalFile,
};

struct ProvenanceSpan {
  ProvenanceSpace space = ProvenanceSpace::kNativeSample;
  std::uint64_t offset = 0;     // first covered byte in this space
  std::uint64_t length = 0;     // number of covered bytes
  std::uint8_t bit_offset = 0;  // meaningful when bit_aligned
  std::uint64_t bit_length = 0; // exact bits; zero for byte-aligned spans
  bool bit_aligned = false;

  bool operator==(const ProvenanceSpan&) const = default;
};

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_PROVENANCE_H
