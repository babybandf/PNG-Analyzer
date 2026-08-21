#ifndef PNGA_UI_QT_SELECTION_BUS_H
#define PNGA_UI_QT_SELECTION_BUS_H

// WP-205: the single Selection controller (ADR-0004). Panels publish with an
// origin id; the bus echoes the change to every subscriber, who ignores events
// carrying their own origin (no update loops). A document generation counter
// drops stale publications so an old document's selection cannot overwrite a
// newer one.

#include <pnga/trace-model/selection.h>

#include <QObject>

namespace pnga::ui::qt {

class SelectionBus final : public QObject {
  Q_OBJECT
 public:
  explicit SelectionBus(QObject* parent = nullptr);

  // Starts a new document: clears the current selection and marks the
  // generation so in-flight publications from older documents are dropped.
  void setDocumentGeneration(std::uint64_t generation) noexcept;

  std::uint64_t documentGeneration() const noexcept { return generation_; }

  // Publishes a selection from panel `origin`. Publications whose generation
  // differs from the current document generation are ignored.
  void publish(int origin, std::uint64_t generation,
               const pnga::trace_model::Selection& selection) noexcept;

  // The latest accepted selection (empty for a fresh document).
  pnga::trace_model::Selection current() const noexcept { return current_; }

 signals:
  // Emitted for every accepted publication, tagged with the publishing panel.
  // Subscribers must ignore events whose origin is their own.
  void selectionChanged(int origin,
                        const pnga::trace_model::Selection& selection);

 private:
  pnga::trace_model::Selection current_;
  std::uint64_t generation_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_SELECTION_BUS_H
