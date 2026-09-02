#ifndef PNGA_UI_QT_COMPRESSION_SELECTION_STORE_H
#define PNGA_UI_QT_COMPRESSION_SELECTION_STORE_H

// WP-5U12B: single owner of the Compression Current mapping, Manual Selection
// and navigation history. The store never publishes to the SelectionBus,
// never submits trace work and never reads documents: an accepted navigation
// is announced once through navigationRequested and the new state once
// through stateChanged. A request serial that was already applied is
// suppressed so navigation echoes cannot loop.

#include <pnga/trace-model/compression_navigation.h>

#include <QObject>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

Q_DECLARE_METATYPE(pnga::trace_model::CompressionSelectionState)
Q_DECLARE_METATYPE(pnga::trace_model::CompressionNavigationTarget)

namespace pnga::ui::qt {

class CompressionSelectionStore final : public QObject {
  Q_OBJECT
 public:
  explicit CompressionSelectionStore(QObject* parent = nullptr);

  const pnga::trace_model::CompressionSelectionState& state() const noexcept;
  const std::vector<pnga::trace_model::CompressionNavigationTarget>& history()
      const noexcept;
  std::optional<std::size_t> historyIndex() const noexcept;

  // Starts a generation: clears old-generation Current, Manual Selection,
  // history and serial tracking, then emits stateChanged once.
  void resetGeneration(std::uint64_t generation);

  // Updates only Current; a same-generation Manual Selection is preserved.
  // Rejects stale generations, invalid source units and empty ranges.
  bool setCurrent(const pnga::trace_model::CompressionCurrentMapping& current);

  // Assigns Manual Selection directly: no history entry, no navigation
  // request, one stateChanged emission.
  bool setManual(const pnga::trace_model::CompressionNavigationTarget& target);

  // Requests a navigation: rejects invalid, stale or duplicate-serial
  // targets, sets only Manual Selection, discards only the forward history
  // branch, appends one history entry and emits navigationRequested and
  // stateChanged once each.
  bool applyNavigation(
      const pnga::trace_model::CompressionNavigationTarget& target);

  bool goBack();
  bool goForward();

 signals:
  void stateChanged(const pnga::trace_model::CompressionSelectionState& state);
  void navigationRequested(
      const pnga::trace_model::CompressionNavigationTarget& target);

 private:
  bool canAcceptNavigation(
      const pnga::trace_model::CompressionNavigationTarget& target) const
      noexcept;

  pnga::trace_model::CompressionSelectionState state_;
  std::vector<pnga::trace_model::CompressionNavigationTarget> history_;
  std::optional<std::size_t> history_index_;
  std::uint64_t last_applied_serial_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_COMPRESSION_SELECTION_STORE_H
