#ifndef PNGA_UI_QT_CHUNK_MODEL_H
#define PNGA_UI_QT_CHUNK_MODEL_H

// WP-104: read-only Qt table model over a physical Chunk index. It exposes the
// envelope fields only; it never parses or copies chunk data.

#include <pnga/png-format/chunk_index.h>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>

namespace pnga::ui::qt {

class ChunkModel final : public QAbstractItemModel {
  Q_OBJECT
 public:
  enum Column { kType = 0, kLength, kDataOffset, kCrcOffset, kColumnCount };

  // The ChunkIndex is borrowed and must outlive the model.
  explicit ChunkModel(const pnga::png_format::ChunkIndex* index,
                      QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  QModelIndex index(int row, int column,
                    const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex&) const override {
    return QModelIndex();
  }

  // Returns the ChunkNode for `row`. Precondition: 0 <= row < rowCount().
  const pnga::png_format::ChunkNode& chunkAt(int row) const;

 private:
  const pnga::png_format::ChunkIndex* index_;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_CHUNK_MODEL_H
