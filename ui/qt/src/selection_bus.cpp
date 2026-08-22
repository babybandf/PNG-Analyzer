// WP-205 SelectionBus implementation.

#include "pnga/ui/qt/selection_bus.h"

namespace pnga::ui::qt {

SelectionBus::SelectionBus(QObject* parent) : QObject(parent) {}

void SelectionBus::setDocumentGeneration(std::uint64_t generation) noexcept {
  generation_ = generation;
  current_ = pnga::trace_model::Selection{};
}

void SelectionBus::publish(int origin, std::uint64_t generation,
                           const pnga::trace_model::Selection& selection) noexcept {
  if (generation != generation_) {
    return;  // stale publication from an older document
  }
  current_ = selection;
  emit selectionChanged(origin, selection);
}

void SelectionBus::publishMerged(
    int origin, std::uint64_t generation,
    const pnga::trace_model::Selection& update) noexcept {
  if (generation != generation_) {
    return;
  }
  current_.merge_with(update);
  emit selectionChanged(origin, current_);
}

}  // namespace pnga::ui::qt
