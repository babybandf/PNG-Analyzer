// Inspection context identity and publication rules for per-frame analysis
// (WP-APNG-INSPECT contract C1). A ticket binds a publication to the
// analysis context that produced it so stale results can be rejected.

#ifndef PNGA_TRACE_MODEL_INSPECTION_CONTEXT_H
#define PNGA_TRACE_MODEL_INSPECTION_CONTEXT_H

#include "pnga/trace-model/selection.h"

#include <cstdint>

namespace pnga::trace_model {

// Immutable identity of one analysis context: the document generation that
// produced it plus the image identity (static or animation frame) analyzed.
struct AnalysisKey {
  std::uint64_t generation = 0;
  ImageIdentity identity = StaticImage{};
  bool operator==(const AnalysisKey&) const = default;
};

// Binds a publication to the analysis context it was computed for. The
// target epoch counts target changes (frame/document switches); the
// selection serial counts committed selection changes. Hover never changes
// a ticket. Increment overflow rejects new requests instead of wrapping.
struct InspectionTicket {
  AnalysisKey key;
  Stage stage = Stage::kUnknown;
  std::uint64_t target_epoch = 0;
  std::uint64_t selection_serial = 0;
  bool operator==(const InspectionTicket&) const = default;
};

enum class PublicationScope { kTarget, kPixel };

// Decides whether `result` may be published into a context whose current
// ticket is `current`. kTarget compares key and target epoch only: encoded
// data is reusable across canvas stages, so stage and selection serial are
// ignored. kPixel additionally requires the same stage and selection
// serial. Both scopes require exact key and epoch equality.
bool accepts_publication(const InspectionTicket& current,
                         const InspectionTicket& result,
                         PublicationScope scope) noexcept;

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_INSPECTION_CONTEXT_H
