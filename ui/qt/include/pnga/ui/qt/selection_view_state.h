#ifndef PNGA_UI_QT_SELECTION_VIEW_STATE_H
#define PNGA_UI_QT_SELECTION_VIEW_STATE_H

// WP-5U1: transient Qt-side view state. These preferences and hover/locked
// presentation coordinates are deliberately separate from the domain
// trace-model::Selection (ADR-0004).

#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <optional>

namespace pnga::ui::qt {

enum class HexSource {
  kFile = 0,
  kIdatStream = 1,
  kInflated = 2,
  kDefiltered = 3,
};

enum class NumericBase { kDecimal = 0, kHexadecimal = 1 };

struct SelectionViewState {
  std::optional<pnga::trace_model::ImageCoordinate> hover;
  std::optional<pnga::trace_model::ImageCoordinate> locked;
  HexSource hex_source = HexSource::kFile;
  NumericBase numeric_base = NumericBase::kDecimal;

  // Returns false and preserves the old value for malformed presentation
  // coordinates. Bounds against a document's dimensions belong to the
  // analysis-engine query.
  bool set_hover(const pnga::trace_model::ImageCoordinate& coordinate) noexcept {
    if (!coordinate.valid()) {
      return false;
    }
    hover = coordinate;
    return true;
  }

  bool set_locked(
      const pnga::trace_model::ImageCoordinate& coordinate) noexcept {
    if (!coordinate.valid()) {
      return false;
    }
    locked = coordinate;
    return true;
  }

  void clear_hover() noexcept { hover.reset(); }
  void clear_locked() noexcept { locked.reset(); }

  // A new document cannot inherit coordinates from an older generation.
  void set_document_generation(std::uint64_t generation) noexcept {
    if (generation != document_generation) {
      document_generation = generation;
      hover.reset();
      locked.reset();
    }
  }

  std::uint64_t document_generation = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_SELECTION_VIEW_STATE_H
