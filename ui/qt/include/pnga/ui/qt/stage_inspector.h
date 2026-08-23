#ifndef PNGA_UI_QT_STAGE_INSPECTOR_H
#define PNGA_UI_QT_STAGE_INSPECTOR_H

// WP-5U7: coordinate-driven reconstruction report. The widget consumes the
// immutable analysis-engine view model and renders a selectable, copyable
// report plus a small dependency-aware neighborhood. It does not parse PNG
// bytes or run reconstruction.

#include "pnga/ui/qt/stage_inspector_model.h"

#include <pnga/trace-model/selection.h>

#include <QWidget>

#include <cstdint>
#include <memory>

class QLabel;
class QTextEdit;

namespace pnga::ui::qt {

class StageInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit StageInspector(QWidget* parent = nullptr);

  void setStageSet(std::shared_ptr<const pnga::analysis_engine::StageSet> set);
  void setDeliveredPixels(std::uint32_t width, std::uint32_t height,
                          std::vector<std::byte> rgba);
  void clear();

  // Keeps the report aligned with the global DEC/HEX presentation setting.
  void setNumericBase(bool hexadecimal);

  StageInspectorModel* model() const noexcept { return model_; }

 public slots:
  void onPixelSelected(std::uint64_t x, std::uint64_t y);
  // Shows the large-file query status in the report without exposing an
  // implementation/debug "row query" line.
  void setRowQueryStatus(const QString& status_text);

 signals:
  // Published when the stage changes, so other panels can react (WP-205).
  void stageChanged(pnga::trace_model::Stage stage);

 private:
  void refreshReport();

  StageInspectorModel* model_ = nullptr;
  QTextEdit* report_ = nullptr;
  std::uint64_t x_ = 0;
  std::uint64_t y_ = 0;
  bool hexadecimal_ = false;
  QString query_status_ = QStringLiteral("not indexed");
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STAGE_INSPECTOR_H
