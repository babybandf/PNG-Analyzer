#ifndef PNGA_UI_QT_HUFFMAN_INSPECTOR_H
#define PNGA_UI_QT_HUFFMAN_INSPECTOR_H

// WP-5U12D: the product Huffman page. A HuffmanInspectorModel backed
// QTableView renders one projected table (no QTableWidget, no per-row
// widgets); the page owns the Block/table selector, the symbol details with
// both bit orders, the Current/Manual Selection coexistence and the typed
// occurrence navigation. Qt never reverses bits or derives facts.

#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/compression_inspector_page.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/huffman_inspector_model.h>

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class QButtonGroup;
class QLabel;
class QPushButton;
class QShowEvent;
class QTableView;
class QWidget;

namespace pnga::ui::qt {

class HuffmanInspectorModel;

class HuffmanInspector final : public CompressionInspectorPage {
  Q_OBJECT
 public:
  // Kept for the WP-5U15 performance gate; the model-backed table itself
  // always shows the complete published table (WP-5U12D).
  static constexpr int kMaxVisibleRows = 4096;
  explicit HuffmanInspector(QWidget* parent = nullptr);

  // Publishes the bounded Huffman projection bundle. The page shows the
  // table of the manually selected block (falling back to the current block
  // and then the first table) for the selected table kind.
  void setView(const pnga::analysis_engine::HuffmanInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::HuffmanInspectorView& view() const noexcept {
    return view_;
  }

  // Attaches the shared WP-5U12B store. Row selection publishes a Manual
  // target through the store (no history entry, no navigation); opening an
  // occurrence applies a typed navigation through it. Without a store the
  // page emits the typed targets instead. The store must outlive the page.
  void setSelectionStore(CompressionSelectionStore* store);
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

 signals:
  void navigationRequested(
      const pnga::trace_model::CompressionNavigationTarget& target);

 private slots:
  void onSelectionChanged();
  void openOccurrence();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  std::optional<const pnga::analysis_engine::HuffmanInspectorTable*>
  activeTable() const noexcept;
  std::optional<int> activeVisibleRow() const noexcept;
  const pnga::analysis_engine::HuffmanOccurrenceFact* occurrenceFact(
      std::uint64_t token_index) const noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  symbolTarget(const pnga::analysis_engine::HuffmanInspectorEntry& entry)
      const noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  occurrenceTarget(const pnga::analysis_engine::HuffmanOccurrenceFact& fact,
                   std::uint16_t symbol) const noexcept;
  std::uint64_t nextRequestSerial() const noexcept;
  void syncActiveTable();
  void syncSelectionFromState();
  void updateButtons();
  void updateDetails();

  pnga::analysis_engine::HuffmanInspectorView view_;
  HuffmanInspectorModel* model_ = nullptr;
  QTableView* table_ = nullptr;
  CompressionSelectionStore* selection_store_ = nullptr;
  QMetaObject::Connection store_connection_;
  QLabel* heading_ = nullptr;
  QWidget* selector_ = nullptr;
  QButtonGroup* kind_buttons_ = nullptr;
  QPushButton* open_occurrence_button_ = nullptr;
  bool hide_zero_bit_entries_ = true;
  bool applying_state_ = false;
  bool splitter_sized_ = false;
  // Cursor into the active entry's bounded occurrence list; the list cycles
  // and no separate occurrence index is created.
  std::size_t occurrence_cursor_ = 0;
  mutable std::uint64_t serial_base_ = 0;
  mutable std::uint64_t serial_counter_ = 0;
  // Per-document column refit policy: content-derived widths re-derive on a
  // generation change or a table-kind switch; same-generation same-kind
  // republishes preserve manual widths.
  std::uint64_t last_refit_generation_ = 0;
  int last_refit_kind_ = -1;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HUFFMAN_INSPECTOR_H
