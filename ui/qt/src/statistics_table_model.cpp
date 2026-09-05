// WP-602G: Statistics page model implementation. Fixed columns Group,
// Metric, Value, Unit, Status; the display role formats the raw quint64
// through the current QLocale (grouping separators included) and renders
// missing data as an em dash; the user role carries the raw quint64 so
// programmatic consumers never depend on locale text. Accessible text
// spells out the value and the section status/scope so partial and error
// states are never encoded by color alone.

#include "pnga/ui/qt/statistics_table_model.h"

#include <QLocale>
#include <QString>

namespace pnga::ui::qt {
namespace {

using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;

QString status_text(SectionStatus status) noexcept {
  switch (status) {
    case SectionStatus::kUnavailable:
      return QStringLiteral("unavailable");
    case SectionStatus::kReady:
      return QStringLiteral("ready");
    case SectionStatus::kPartial:
      return QStringLiteral("partial");
    case SectionStatus::kCancelled:
      return QStringLiteral("cancelled");
    case SectionStatus::kBudgetExceeded:
      return QStringLiteral("budget_exceeded");
    case SectionStatus::kInvalidInput:
      return QStringLiteral("invalid_input");
    case SectionStatus::kOverflow:
      return QStringLiteral("overflow");
    case SectionStatus::kError:
      return QStringLiteral("error");
  }
  return QStringLiteral("error");
}

QString scope_text(SectionScope scope) noexcept {
  switch (scope) {
    case SectionScope::kNone:
      return QStringLiteral("none");
    case SectionScope::kWholeDocument:
      return QStringLiteral("whole_document");
    case SectionScope::kVerifiedPrefix:
      return QStringLiteral("verified_prefix");
  }
  return QStringLiteral("none");
}

QString value_text(const StatisticsRow& row) {
  if (!row.value.has_value()) {
    return QStringLiteral("\u2014");
  }
  return QLocale().toString(static_cast<qulonglong>(*row.value));
}

}  // namespace

StatisticsTableModel::StatisticsTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void StatisticsTableModel::setRows(
    std::shared_ptr<const std::vector<StatisticsRow>> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
}

const StatisticsRow* StatisticsTableModel::rowAt(int row) const noexcept {
  if (rows_ == nullptr || row < 0 ||
      row >= static_cast<int>(rows_->size())) {
    return nullptr;
  }
  return &(*rows_)[static_cast<std::size_t>(row)];
}

int StatisticsTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return rows_ == nullptr ? 0 : static_cast<int>(rows_->size());
}

int StatisticsTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant StatisticsTableModel::data(const QModelIndex& index, int role) const {
  const StatisticsRow* row = rowAt(index.row());
  if (row == nullptr) {
    return {};
  }
  const Column column = static_cast<Column>(index.column());
  switch (role) {
    case Qt::DisplayRole:
      switch (column) {
        case Group:
          return QString::fromStdString(row->group);
        case Metric:
          return QString::fromStdString(row->label);
        case Value:
          return value_text(*row);
        case Unit:
          return QString::fromStdString(row->unit);
        case Status:
          return status_text(row->state.status);
        case ColumnCount:
          break;
      }
      break;
    case RawValueRole:
      if (column == Value && row->value.has_value()) {
        return QVariant::fromValue<quint64>(*row->value);
      }
      return {};
    case Qt::AccessibleTextRole: {
      QString text = QStringLiteral("%1: %2").arg(
          QString::fromStdString(row->label), value_text(*row));
      if (!row->unit.empty()) {
        text += QStringLiteral(" %1").arg(QString::fromStdString(row->unit));
      }
      text += QStringLiteral(" (%1)").arg(status_text(row->state.status));
      return text;
    }
    case Qt::AccessibleDescriptionRole:
      return QStringLiteral("section status %1, scope %2")
          .arg(status_text(row->state.status), scope_text(row->state.scope));
    default:
      break;
  }
  return {};
}

QVariant StatisticsTableModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (static_cast<Column>(section)) {
    case Group:
      return QStringLiteral("Group");
    case Metric:
      return QStringLiteral("Metric");
    case Value:
      return QStringLiteral("Value");
    case Unit:
      return QStringLiteral("Unit");
    case Status:
      return QStringLiteral("Status");
    case ColumnCount:
      break;
  }
  return {};
}

}  // namespace pnga::ui::qt
