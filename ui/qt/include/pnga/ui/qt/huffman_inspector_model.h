#ifndef PNGA_UI_QT_HUFFMAN_INSPECTOR_MODEL_H
#define PNGA_UI_QT_HUFFMAN_INSPECTOR_MODEL_H

// WP-5U12D: model behind the Huffman table. The model exposes one immutable
// projected HuffmanInspectorTable through QAbstractTableModel with lazy
// per-cell formatting, hides zero-bit entries by default and renders the
// immutable Current mapping (the entry associated with the selected token)
// and the user-owned Manual Selection as data roles. It formats immutable
// facts only: no PNG/DEFLATE parsing, no bit reversal, no trace queries.

#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/trace-model/compression_navigation.h>

#include <QAbstractTableModel>

#include <memory>
#include <vector>

Q_DECLARE_METATYPE(pnga::analysis_engine::HuffmanInspectorEntry)

namespace pnga::ui::qt {

// Data roles on every Huffman model index. Prefixed to stay distinct from
// the BlockInspectorModel roles sharing this namespace.
enum HuffmanInspectorRole {
  HuffmanEntryRole = Qt::UserRole + 1,
  HuffmanContainsCurrentRole,
  HuffmanIsManualSelectionRole,
  HuffmanAccessibleTextRole,
};

class HuffmanInspectorModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column {
    Symbol = 0,
    Meaning,
    Bits,
    Canonical,
    ReadOrder,
    UsesInResult,
    ColumnCount,
  };

  explicit HuffmanInspectorModel(QObject* parent = nullptr);

  // Publishes one projected table. A null pointer clears the rows while the
  // zero-bit filter and selection state are retained.
  void setTable(std::shared_ptr<const pnga::analysis_engine::
                                   HuffmanInspectorTable>
                    table);

  // Zero-bit entries stay hidden by default; the retained entries remain
  // available to tests and details.
  void setHideZeroBitEntries(bool hide);

  // Replaces the Current/Manual Selection state used by the roles.
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

  // Borrowed entry behind the given visible row; null when out of range or
  // hidden by the zero-bit filter. The pointer is valid until the next
  // setTable call.
  const pnga::analysis_engine::HuffmanInspectorEntry* entryAt(
      int visible_row) const noexcept;

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
  int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  const std::vector<pnga::analysis_engine::HuffmanInspectorEntry>&
  entries() const noexcept;
  bool isManualSelection(
      const pnga::analysis_engine::HuffmanInspectorEntry& entry) const
      noexcept;

  std::shared_ptr<const pnga::analysis_engine::HuffmanInspectorTable> table_;
  pnga::trace_model::CompressionSelectionState state_{};
  bool hide_zero_bit_entries_ = true;
  // Visible entry positions of the published table.
  std::vector<int> visible_;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HUFFMAN_INSPECTOR_MODEL_H
