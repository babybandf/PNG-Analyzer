#include <pnga/ui/qt/compression_selection_store.h>

namespace pnga::ui::qt {

CompressionSelectionStore::CompressionSelectionStore(QObject* parent)
    : QObject(parent) {
  // QSignalSpy and queued signal connections copy these value types.
  qRegisterMetaType<pnga::trace_model::CompressionSelectionState>();
  qRegisterMetaType<pnga::trace_model::CompressionNavigationTarget>();
}

const pnga::trace_model::CompressionSelectionState&
CompressionSelectionStore::state() const noexcept {
  return state_;
}

const std::vector<pnga::trace_model::CompressionNavigationTarget>&
CompressionSelectionStore::history() const noexcept {
  return history_;
}

std::optional<std::size_t> CompressionSelectionStore::historyIndex()
    const noexcept {
  return history_index_;
}

void CompressionSelectionStore::resetGeneration(std::uint64_t generation) {
  state_ = pnga::trace_model::CompressionSelectionState{};
  state_.generation = generation;
  history_.clear();
  history_index_.reset();
  last_applied_serial_ = 0;
  emit stateChanged(state_);
}

bool CompressionSelectionStore::setCurrent(
    const pnga::trace_model::CompressionCurrentMapping& current) {
  if (current.generation != state_.generation ||
      !current.source_unit.valid() || !current.output_range.valid() ||
      current.output_range.empty()) {
    return false;
  }
  state_.current = current;
  emit stateChanged(state_);
  return true;
}

bool CompressionSelectionStore::canAcceptNavigation(
    const pnga::trace_model::CompressionNavigationTarget& target) const
    noexcept {
  return target.valid() && target.generation == state_.generation &&
         target.request_serial != last_applied_serial_;
}

bool CompressionSelectionStore::setManual(
    const pnga::trace_model::CompressionNavigationTarget& target) {
  if (!canAcceptNavigation(target)) {
    return false;
  }
  state_.manual = target;
  last_applied_serial_ = target.request_serial;
  emit stateChanged(state_);
  return true;
}

bool CompressionSelectionStore::applyNavigation(
    const pnga::trace_model::CompressionNavigationTarget& target) {
  if (!canAcceptNavigation(target)) {
    return false;
  }
  state_.manual = target;
  last_applied_serial_ = target.request_serial;
  if (history_index_.has_value()) {
    const std::size_t forward = *history_index_ + 1;
    if (forward < history_.size()) {
      history_.resize(forward);  // discard only the forward branch
    }
  }
  history_.push_back(target);
  history_index_ = history_.size() - 1;
  emit navigationRequested(target);
  emit stateChanged(state_);
  return true;
}

bool CompressionSelectionStore::goBack() {
  if (!history_index_.has_value() || *history_index_ == 0) {
    return false;
  }
  --*history_index_;
  const pnga::trace_model::CompressionNavigationTarget& target =
      history_[*history_index_];
  state_.manual = target;
  last_applied_serial_ = target.request_serial;
  emit navigationRequested(target);
  emit stateChanged(state_);
  return true;
}

bool CompressionSelectionStore::goForward() {
  if (!history_index_.has_value() || *history_index_ + 1 >= history_.size()) {
    return false;
  }
  ++*history_index_;
  const pnga::trace_model::CompressionNavigationTarget& target =
      history_[*history_index_];
  state_.manual = target;
  last_applied_serial_ = target.request_serial;
  emit navigationRequested(target);
  emit stateChanged(state_);
  return true;
}

}  // namespace pnga::ui::qt
