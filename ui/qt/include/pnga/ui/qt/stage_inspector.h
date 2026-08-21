#ifndef PNGA_UI_QT_STAGE_INSPECTOR_H
#define PNGA_UI_QT_STAGE_INSPECTOR_H

// WP-306: stage inspector widget — a scrubber (Filtered / Unfiltered / Native /
// Delivered) over a StageInspectorModel, a value table for the selected pixel
// and a detail line with the per-byte filter formula. Pure Qt UI; all data
// comes from the analysis engine.

#include "pnga/ui/qt/stage_inspector_model.h"

#include <pnga/trace-model/selection.h>

#include <QWidget>

#include <cstdint>
#include <memory>

class QComboBox;
class QLabel;
class QTableView;
class QVBoxLayout;

namespace pnga::ui::qt {

class StageInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit StageInspector(QWidget* parent = nullptr);

  void setStageSet(std::shared_ptr<const pnga::analysis_engine::StageSet> set);
  void setDeliveredPixels(std::uint32_t width, std::uint32_t height,
                          std::vector<std::byte> rgba);
  void clear();

  StageInspectorModel* model() const noexcept { return model_; }

 public slots:
  void onPixelSelected(std::uint64_t x, std::uint64_t y);
  // WP-406: shows the large-file query status (indexed/replaying/ready/error)
  // for the row under inspection.
  void setRowQueryStatus(const QString& status_text);

 signals:
  // Published when the stage changes, so other panels can react (WP-205).
  void stageChanged(pnga::trace_model::Stage stage);

 private:
  void onStageChanged(int index);
  void onCurrentCellChanged(const QModelIndex& current,
                            const QModelIndex& previous);
  void refreshDetail();

  StageInspectorModel* model_ = nullptr;
  QComboBox* stage_combo_ = nullptr;
  QTableView* table_ = nullptr;
  QLabel* detail_ = nullptr;
  QLabel* query_status_label_ = nullptr;
  std::uint64_t x_ = 0;
  std::uint64_t y_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STAGE_INSPECTOR_H
