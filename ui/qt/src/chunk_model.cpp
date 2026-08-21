// WP-104 ChunkModel implementation.

#include "pnga/ui/qt/chunk_model.h"

#include <QString>

namespace pnga::ui::qt {

namespace {

QVariant u64_variant(std::uint64_t value) {
  return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(value));
}

}  // namespace

ChunkModel::ChunkModel(const pnga::png_format::ChunkIndex* index,
                       QObject* parent)
    : QAbstractItemModel(parent), index_(index) {}

int ChunkModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0
                          : static_cast<int>(index_->chunks.size());
}

int ChunkModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

QModelIndex ChunkModel::index(int row, int column,
                              const QModelIndex& parent) const {
  if (parent.isValid() || row < 0 || row >= rowCount() ||
      column < 0 || column >= kColumnCount) {
    return QModelIndex();
  }
  return createIndex(row, column);
}

QVariant ChunkModel::data(const QModelIndex& idx, int role) const {
  if (!idx.isValid() ||
      idx.row() >= static_cast<int>(index_->chunks.size())) {
    return QVariant();
  }
  const auto& node =
      index_->chunks[static_cast<std::size_t>(idx.row())];
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  switch (idx.column()) {
    case kType:
      return QString::fromStdString(node.text());
    case kLength:
      return u64_variant(node.data_length);
    case kDataOffset:
      return u64_variant(node.data_offset);
    case kCrcOffset:
      return u64_variant(node.crc_offset);
    default:
      return QVariant();
  }
}

QVariant ChunkModel::headerData(int section, Qt::Orientation orientation,
                                int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
    return QVariant();
  }
  switch (section) {
    case kType:
      return QStringLiteral("Type");
    case kLength:
      return QStringLiteral("Length");
    case kDataOffset:
      return QStringLiteral("Data @");
    case kCrcOffset:
      return QStringLiteral("CRC @");
    default:
      return QVariant();
  }
}

const pnga::png_format::ChunkNode& ChunkModel::chunkAt(int row) const {
  return index_->chunks[static_cast<std::size_t>(row)];
}

}  // namespace pnga::ui::qt
