#include "pnga/trace-model/inspection_context.h"

namespace pnga::trace_model {

bool accepts_publication(const InspectionTicket& current,
                         const InspectionTicket& result,
                         PublicationScope scope) noexcept {
  if (!(current.key == result.key)) {
    return false;
  }
  if (current.target_epoch != result.target_epoch) {
    return false;
  }
  if (scope == PublicationScope::kPixel) {
    if (current.stage != result.stage) {
      return false;
    }
    if (current.selection_serial != result.selection_serial) {
      return false;
    }
  }
  return true;
}

}  // namespace pnga::trace_model
