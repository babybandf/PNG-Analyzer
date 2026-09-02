// WP-5U12D Huffman model implementation: lazy formatting of immutable
// projection facts. Bit strings are rendered exactly as the Qt-free
// projection produced them; Qt never reverses or re-computes bits.

#include "pnga/ui/qt/huffman_inspector_model.h"

#include <pnga/ui/qt/application_theme.h>

#include <QBrush>

namespace pnga::ui::qt {

namespace {

QString entry_text(const std::string& text) {
  return QString::fromStdString(text);
}

QString bit_text(const std::string& bits) {
  if (bits.empty()) {
    return QStringLiteral("—");  // zero-bit entry without a canonical code
  }
  return QString::fromStdString(bits);
}

}  // namespace

HuffmanInspectorModel::HuffmanInspectorModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void HuffmanInspectorModel::setTable(
    std::shared_ptr<const pnga::analysis_engine::HuffmanInspectorTable>
        table) {
  beginResetModel();
  table_ = std::move(table);
  visible_.clear();
  if (table_ != nullptr) {
    const auto& all = table_->entries;
    visible_.reserve(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (!hide_zero_bit_entries_ || all[i].bit_length != 0) {
        visible_.push_back(static_cast<int>(i));
      }
    }
  }
  endResetModel();
}

void HuffmanInspectorModel::setHideZeroBitEntries(bool hide) {
  if (hide_zero_bit_entries_ == hide) {
    return;
  }
  beginResetModel();
  hide_zero_bit_entries_ = hide;
  visible_.clear();
  if (table_ != nullptr) {
    const auto& all = table_->entries;
    visible_.reserve(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (!hide_zero_bit_entries_ || all[i].bit_length != 0) {
        visible_.push_back(static_cast<int>(i));
      }
    }
  }
  endResetModel();
}

void HuffmanInspectorModel::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  state_ = state;
  if (rowCount() > 0 && columnCount() > 0) {
    const QModelIndex top = index(0, 0);
    const QModelIndex bottom = index(rowCount() - 1, ColumnCount - 1);
    emit dataChanged(top, bottom,
                     {Qt::DisplayRole, Qt::BackgroundRole,
                      HuffmanContainsCurrentRole, HuffmanIsManualSelectionRole,
                      HuffmanAccessibleTextRole});
  }
}

const std::vector<pnga::analysis_engine::HuffmanInspectorEntry>&
HuffmanInspectorModel::entries() const noexcept {
  static const std::vector<pnga::analysis_engine::HuffmanInspectorEntry>
      empty;
  return table_ != nullptr ? table_->entries : empty;
}

const pnga::analysis_engine::HuffmanInspectorEntry*
HuffmanInspectorModel::entryAt(int visible_row) const noexcept {
  if (visible_row < 0 || visible_row >= static_cast<int>(visible_.size())) {
    return nullptr;
  }
  const int entry_index = visible_[static_cast<std::size_t>(visible_row)];
  return &entries()[static_cast<std::size_t>(entry_index)];
}

int HuffmanInspectorModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || table_ == nullptr) {
    return 0;
  }
  return static_cast<int>(visible_.size());
}

int HuffmanInspectorModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return ColumnCount;
}

bool HuffmanInspectorModel::isManualSelection(
    const pnga::analysis_engine::HuffmanInspectorEntry& entry) const noexcept {
  if (table_ == nullptr || !state_.manual.has_value() ||
      !state_.manual->block_index.has_value() ||
      !state_.manual->symbol.has_value()) {
    return false;
  }
  return *state_.manual->block_index == table_->block_index &&
         *state_.manual->symbol == entry.symbol;
}

QVariant HuffmanInspectorModel::data(const QModelIndex& index, int role)
    const {
  const auto* entry = entryAt(index.row());
  if (entry == nullptr || index.column() < 0 ||
      index.column() >= ColumnCount) {
    return {};
  }
  const bool contains_current = entry->selected;
  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case Symbol:
          return QString::number(entry->symbol);
        case Meaning:
          return entry_text(entry->meaning);
        case Bits:
          return QString::number(entry->bit_length);
        case Canonical:
          return bit_text(entry->canonical_bits);
        case ReadOrder:
          return bit_text(entry->read_order_bits);
        case UsesInResult:
          return QString::number(
              static_cast<qulonglong>(entry->occurrence_token_indices.size()));
        default:
          return {};
      }
    case Qt::ToolTipRole:
      if (index.column() == UsesInResult && table_ != nullptr) {
        return QStringLiteral(
                   "Uses in the current bounded trace result of Block #%1; "
                   "not a whole-block count.")
            .arg(static_cast<qulonglong>(table_->block_index));
      }
      return {};
    case Qt::FontRole:
      // Numeric/bit cells use the shared monospace token (flow-ui §14).
      switch (index.column()) {
        case Symbol:
        case Bits:
        case Canonical:
        case ReadOrder:
        case UsesInResult:
          return ApplicationTheme::applicationMonospaceFont();
        default:
          return {};
      }
    case Qt::BackgroundRole:
      // Centralized theme token; never a hard-coded RGB literal. The
      // Current marker must stay visually distinct from the native row
      // selection (flow-ui §20.6).
      if (contains_current) {
        const QColor current = ApplicationTheme::applicationColor(
            ApplicationTheme::ColorToken::kCurrentPixel);
        if (current.isValid()) {
          return QBrush(current);
        }
      }
      return {};
    case HuffmanEntryRole:
      return QVariant::fromValue(*entry);
    case HuffmanContainsCurrentRole:
      return contains_current;
    case HuffmanIsManualSelectionRole:
      return isManualSelection(*entry);
    case HuffmanAccessibleTextRole: {
      QString text = QStringLiteral("Huffman symbol %1, %2, %3 bits")
                         .arg(entry->symbol)
                         .arg(entry_text(entry->meaning))
                         .arg(entry->bit_length);
      if (contains_current) {
        text += QStringLiteral(", associated with the current token");
      }
      if (isManualSelection(*entry)) {
        text += QStringLiteral(", manually selected");
      }
      return text;
    }
    default:
      return {};
  }
}

QVariant HuffmanInspectorModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  switch (section) {
    case Symbol:
      return QStringLiteral("Symbol");
    case Meaning:
      return QStringLiteral("Meaning");
    case Bits:
      return QStringLiteral("Bits");
    case Canonical:
      return QStringLiteral("Canonical");
    case ReadOrder:
      return QStringLiteral("Read order");
    case UsesInResult:
      return QStringLiteral("Uses in result");
    default:
      return {};
  }
}

}  // namespace pnga::ui::qt
