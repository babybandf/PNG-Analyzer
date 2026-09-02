#ifndef PNGA_UI_QT_BLOCK_INSPECTOR_MODEL_H
#define PNGA_UI_QT_BLOCK_INSPECTOR_MODEL_H

// WP-5U12C: model behind the DEFLATE Blocks table. The model exposes the
// complete generation-level Fast Compression Index through QAbstractTableModel
// with lazy per-cell formatting, plus the immutable Current mapping and the
// user-owned Manual Selection as data roles. It formats immutable facts only:
// no PNG/DEFLATE parsing, no decoding, no trace queries.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/trace-model/compression_navigation.h>

#include <QAbstractTableModel>

#include <memory>

Q_DECLARE_METATYPE(pnga::trace_model::ZlibBitRange)
Q_DECLARE_METATYPE(pnga::trace_model::InflatedByteRange)
Q_DECLARE_METATYPE(std::vector<pnga::trace_model::ProvenanceSpan>)

namespace pnga::ui::qt {

// Data roles on every Blocks model index.
enum BlockInspectorRole {
  BlockIndexRole = Qt::UserRole + 1,
  InputRangeRole,
  OutputRangeRole,
  PhysicalSpansRole,
  ContainsCurrentRole,
  IsManualSelectionRole,
  AccessibleTextRole,
};

class BlockInspectorModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column {
    Current = 0,
    Number,
    Type,
    Final,
    InputBits,
    OutputBytes,
    Events,
    Scanlines,
    ColumnCount,
  };

  explicit BlockInspectorModel(QObject* parent = nullptr);

  // Publishes the complete Fast Index. Rows are never truncated; a null
  // pointer clears the table while the selection state is retained.
  void setIndex(
      std::shared_ptr<const pnga::analysis_engine::FastCompressionIndexView>
          index);

  // Replaces the Current/Manual Selection state used by the roles. A state
  // from another generation than the published index highlights nothing.
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

  // Borrowed row for the given table row; null when out of range. The view
  // must not outlive the index published through setIndex.
  const pnga::analysis_engine::FastCompressionBlockRow* rowAt(
      int row) const noexcept;

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
  int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  bool containsCurrent(
      const pnga::analysis_engine::FastCompressionBlockRow& row) const noexcept;
  bool isManualSelection(
      const pnga::analysis_engine::FastCompressionBlockRow& row) const noexcept;

  std::shared_ptr<const pnga::analysis_engine::FastCompressionIndexView>
      index_;
  pnga::trace_model::CompressionSelectionState state_{};
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_BLOCK_INSPECTOR_MODEL_H
