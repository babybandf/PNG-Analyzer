#ifndef PNGA_UI_QT_DECODE_TRACE_MODEL_H
#define PNGA_UI_QT_DECODE_TRACE_MODEL_H

// WP-5U12E: model behind the Decode Trace table. The model exposes one
// immutable bounded DecodeTraceInspectorView through QAbstractTableModel with
// lazy per-cell formatting, the exact Current | Step | Input bits | Event |
// Output column order, and the immutable Current mapping plus the user-owned
// Manual Selection as data roles. It formats immutable facts only: no
// PNG/DEFLATE parsing, no decoding, no trace queries.

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/trace-model/compression_navigation.h>

#include <QAbstractTableModel>

#include <memory>

Q_DECLARE_METATYPE(pnga::analysis_engine::DecodeTraceStep)

namespace pnga::ui::qt {

// Data roles on every Decode Trace model index. Prefixed to stay distinct
// from the Blocks and Huffman roles sharing this namespace.
enum DecodeTraceRole {
  DecodeTraceStepRole = Qt::UserRole + 1,
  DecodeTraceContainsCurrentRole,
  DecodeTraceIsManualSelectionRole,
  DecodeTraceAccessibleTextRole,
};

class DecodeTraceModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { Current = 0, Step, InputBits, Event, Output, ColumnCount };

  explicit DecodeTraceModel(QObject* parent = nullptr);

  // Publishes one bounded projection. A null pointer clears the rows while
  // the selection state is retained.
  void setView(
      std::shared_ptr<const pnga::analysis_engine::DecodeTraceInspectorView>
          view);

  // Replaces the Current/Manual Selection state used by the roles. A state
  // from another generation than the published view highlights nothing.
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

  // Borrowed step behind the given row; null when out of range. The pointer
  // stays valid until the next setView call.
  const pnga::analysis_engine::DecodeTraceStep* stepAt(int row) const
      noexcept;

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
  int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  bool containsCurrent(
      const pnga::analysis_engine::DecodeTraceStep& step) const noexcept;
  bool isManualSelection(
      const pnga::analysis_engine::DecodeTraceStep& step) const noexcept;

  std::shared_ptr<const pnga::analysis_engine::DecodeTraceInspectorView>
      view_;
  pnga::trace_model::CompressionSelectionState state_{};
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_DECODE_TRACE_MODEL_H
