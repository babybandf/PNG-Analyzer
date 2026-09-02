// WP-5U12C Blocks model implementation: lazy formatting of immutable Fast
// Index facts. Cells are formatted on demand; nothing precomputes strings
// per row and no Qt code replays, decodes or guesses geometry.

#include "pnga/ui/qt/block_inspector_model.h"

#include <pnga/ui/qt/application_theme.h>

#include <QBrush>

namespace pnga::ui::qt {

namespace {

QString range_text(std::uint64_t begin, std::uint64_t end) {
  return QStringLiteral("%1–%2")
      .arg(static_cast<qulonglong>(begin))
      .arg(static_cast<qulonglong>(end));
}

QString optional_range_text(const std::optional<std::uint64_t>& first,
                            const std::optional<std::uint64_t>& last) {
  if (first.has_value() && last.has_value()) {
    return range_text(*first, *last);
  }
  return QStringLiteral("—");
}

}  // namespace

BlockInspectorModel::BlockInspectorModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void BlockInspectorModel::setIndex(
    std::shared_ptr<const pnga::analysis_engine::FastCompressionIndexView>
        index) {
  beginResetModel();
  index_ = std::move(index);
  endResetModel();
}

void BlockInspectorModel::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  state_ = state;
  if (rowCount() > 0 && columnCount() > 0) {
    const QModelIndex top = index(0, 0);
    const QModelIndex bottom = index(rowCount() - 1, ColumnCount - 1);
    emit dataChanged(top, bottom,
                     {Qt::DisplayRole, Qt::BackgroundRole,
                      ContainsCurrentRole, IsManualSelectionRole,
                      AccessibleTextRole});
  }
}

const pnga::analysis_engine::FastCompressionBlockRow*
BlockInspectorModel::rowAt(int row) const noexcept {
  if (index_ == nullptr || row < 0 ||
      row >= static_cast<int>(index_->blocks.size())) {
    return nullptr;
  }
  return &index_->blocks[static_cast<std::size_t>(row)];
}

int BlockInspectorModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || index_ == nullptr) {
    return 0;
  }
  return static_cast<int>(index_->blocks.size());
}

int BlockInspectorModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return ColumnCount;
}

bool BlockInspectorModel::containsCurrent(
    const pnga::analysis_engine::FastCompressionBlockRow& row) const noexcept {
  if (index_ == nullptr || !state_.current.has_value() ||
      state_.generation != index_->generation ||
      !state_.current->block_index.has_value()) {
    return false;
  }
  return *state_.current->block_index == row.block_index;
}

bool BlockInspectorModel::isManualSelection(
    const pnga::analysis_engine::FastCompressionBlockRow& row) const noexcept {
  if (index_ == nullptr || !state_.manual.has_value() ||
      state_.generation != index_->generation ||
      !state_.manual->block_index.has_value()) {
    return false;
  }
  return *state_.manual->block_index == row.block_index;
}

QVariant BlockInspectorModel::data(const QModelIndex& index, int role) const {
  const pnga::analysis_engine::FastCompressionBlockRow* row =
      rowAt(index.row());
  if (row == nullptr || index.column() < 0 ||
      index.column() >= ColumnCount) {
    return {};
  }
  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case Current:
          return containsCurrent(*row) ? QStringLiteral("●") : QString();
        case Number:
          return QString::number(
              static_cast<qulonglong>(row->block_index));
        case Type:
          return QLatin1String(
              pnga::deflate_index::block_type_text(row->type));
        case Final:
          return row->last ? QStringLiteral("yes") : QStringLiteral("no");
        case InputBits:
          return range_text(row->input_range.begin.value,
                            row->input_range.end.value);
        case OutputBytes:
          return range_text(row->output_range.begin.value,
                            row->output_range.end.value);
        case Events:
          return row->event_count.has_value()
                     ? QString::number(
                           static_cast<qulonglong>(*row->event_count))
                     : QStringLiteral("—");
        case Scanlines:
          return optional_range_text(row->first_scanline,
                                     row->last_scanline);
        default:
          return {};
      }
    case Qt::FontRole:
      // Numeric/bit cells use the shared monospace token (flow-ui §14).
      switch (index.column()) {
        case Number:
        case InputBits:
        case OutputBytes:
        case Events:
        case Scanlines:
          return ApplicationTheme::applicationMonospaceFont();
        default:
          return {};
      }
    case Qt::BackgroundRole:
      // Centralized theme token; never a hard-coded RGB literal. The Current
      // marker must stay visually distinct from the native row selection.
      if (containsCurrent(*row)) {
        const QColor current =
            ApplicationTheme::applicationColor(
                ApplicationTheme::ColorToken::kCurrentPixel);
        if (current.isValid()) {
          return QBrush(current);
        }
      }
      return {};
    case BlockIndexRole:
      return QVariant::fromValue<qulonglong>(
          static_cast<qulonglong>(row->block_index));
    case InputRangeRole:
      return QVariant::fromValue(row->input_range);
    case OutputRangeRole:
      return QVariant::fromValue(row->output_range);
    case PhysicalSpansRole:
      return QVariant::fromValue(row->physical_spans);
    case ContainsCurrentRole:
      return containsCurrent(*row);
    case IsManualSelectionRole:
      return isManualSelection(*row);
    case AccessibleTextRole: {
      QString text = QStringLiteral("DEFLATE block %1, %2")
                         .arg(static_cast<qulonglong>(row->block_index))
                         .arg(QLatin1String(
                             pnga::deflate_index::block_type_text(row->type)));
      if (row->last) {
        text += QStringLiteral(", final");
      }
      if (containsCurrent(*row)) {
        text += QStringLiteral(", contains current output");
      }
      if (isManualSelection(*row)) {
        text += QStringLiteral(", manually selected");
      }
      return text;
    }
    default:
      return {};
  }
}

QVariant BlockInspectorModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  switch (section) {
    case Current:
      return QStringLiteral("Current");
    case Number:
      return QStringLiteral("#");
    case Type:
      return QStringLiteral("Type");
    case Final:
      return QStringLiteral("Final");
    case InputBits:
      return QStringLiteral("Input bits");
    case OutputBytes:
      return QStringLiteral("Output bytes");
    case Events:
      return QStringLiteral("Events");
    case Scanlines:
      return QStringLiteral("Scanlines");
    default:
      return {};
  }
}

}  // namespace pnga::ui::qt
