#ifndef PNGA_UI_QT_STATISTICS_INSPECTOR_H
#define PNGA_UI_QT_STATISTICS_INSPECTOR_H

// WP-602G: the Statistics inspector (ruling R11). Four fixed inner pages —
// Overview | Chunks | Filters | DEFLATE — present the immutable Task 6 rows
// through model-backed tables; the DEFLATE page shows Blocks/Tokens/Lengths/
// Distances as grouped rows instead of a fifth inner page. The actions
// Refresh, Cancel, Export JSON, Export CSV and Show occurrence emit typed
// signals only; navigation travels through the existing typed pipeline
// (ruling R12) and the widget never parses or decodes PNG data.

#include <pnga/analysis-engine/statistics_view.h>

#include <QWidget>

#include <cstdint>
#include <memory>

Q_DECLARE_METATYPE(pnga::analysis_engine::StatisticsNavigationRequest)

class QKeyEvent;
class QLabel;
class QPushButton;
class QTableView;
class QTabWidget;

namespace pnga::ui::qt {

class StatisticsTableModel;

using pnga::analysis_engine::StatisticsView;

// Export targets carried by exportRequested(int) in fixed order.
enum class StatisticsExportFormat { kJson = 0, kCsv = 1 };

class StatisticsInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit StatisticsInspector(QWidget* parent = nullptr);

  // Publishes one immutable view; selection is preserved by stable row id
  // across progress snapshots and columns re-fit only on a generation change.
  void setView(std::shared_ptr<const StatisticsView> view);

  // One-line progress/status copy (throttled by the publisher).
  void setProgress(QString text);

  // Clears every page, disables actions and restores the initial copy.
  void clear();

 protected:
  // Escape anywhere in the inspector cancels the running collection.
  void keyPressEvent(QKeyEvent* event) override;

 signals:
  void refreshRequested();
  void cancelRequested();
  void exportRequested(int format);
  void occurrenceRequested(
      pnga::analysis_engine::StatisticsNavigationRequest request);

 private:
  struct PageTable {
    StatisticsTableModel* model = nullptr;
    QTableView* table = nullptr;
  };

  void requestOccurrenceFromPage(int page);
  void updateOccurrenceButton();
  void refitColumns();

  PageTable pages_[4];
  QString selected_row_ids_[4];
  QTabWidget* pages_widget_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QPushButton* export_json_button_ = nullptr;
  QPushButton* export_csv_button_ = nullptr;
  QPushButton* occurrence_button_ = nullptr;
  QLabel* progress_label_ = nullptr;
  QString initial_progress_text_;
  std::shared_ptr<const StatisticsView> view_;
  bool has_view_ = false;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STATISTICS_INSPECTOR_H
