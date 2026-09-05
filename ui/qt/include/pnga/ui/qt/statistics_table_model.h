#ifndef PNGA_UI_QT_STATISTICS_TABLE_MODEL_H
#define PNGA_UI_QT_STATISTICS_TABLE_MODEL_H

// WP-602G: lazy Qt formatting of the immutable Statistics rows (WP-602F).
// The model publishes one page's shared row vector through QAbstractTableModel
// and formats immutable facts only: raw integers stay on the user role while
// the display role applies the current QLocale; missing data renders as an
// unavailable placeholder, never a ready zero. No PNG/DEFLATE parsing, no
// decoding, no occurrence queries live here.

#include <pnga/analysis-engine/statistics_view.h>

#include <QAbstractTableModel>

#include <memory>

namespace pnga::ui::qt {

using pnga::analysis_engine::StatisticsRow;

// Data roles on every Statistics model index.
enum StatisticsModelRole {
  RawValueRole = Qt::UserRole + 1,  // raw quint64, invalid when unavailable
};

class StatisticsTableModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column {
    Group = 0,
    Metric,
    Value,
    Unit,
    Status,
    ColumnCount,
  };

  explicit StatisticsTableModel(QObject* parent = nullptr);

  // Publishes one immutable page row set. A null pointer clears the table.
  void setRows(std::shared_ptr<const std::vector<StatisticsRow>> rows);

  // Borrowed row for the given table row; null when out of range. The row
  // must not outlive the vector published through setRows.
  const StatisticsRow* rowAt(int row) const noexcept;

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
  int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

 private:
  std::shared_ptr<const std::vector<StatisticsRow>> rows_;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STATISTICS_TABLE_MODEL_H
