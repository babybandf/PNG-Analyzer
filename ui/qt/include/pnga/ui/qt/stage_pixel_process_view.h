#ifndef PNGA_UI_QT_STAGE_PIXEL_PROCESS_VIEW_H
#define PNGA_UI_QT_STAGE_PIXEL_PROCESS_VIEW_H

// WP-5U9: shared central stage explanation widget.  It only renders the
// bounded, immutable projection supplied by the analysis engine.

#include <pnga/analysis-engine/stage_pixel_process_view.h>

#include <QWidget>

#include <cstdint>
#include <memory>

class QTextEdit;

namespace pnga::ui::qt {

class StagePixelProcessView final : public QWidget {
  Q_OBJECT
 public:
  explicit StagePixelProcessView(
      pnga::analysis_engine::StagePixelProcessStage stage,
      QWidget* parent = nullptr);

  void setStageSet(
      std::shared_ptr<const pnga::analysis_engine::StageSet> stages);
  void clear();
  void setCoordinate(std::uint64_t x, std::uint64_t y);
  void setNumericBase(bool hexadecimal);

 private:
  void refresh();

  pnga::analysis_engine::StagePixelProcessStage stage_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stages_;
  QTextEdit* text_ = nullptr;
  std::uint64_t x_ = 0;
  std::uint64_t y_ = 0;
  bool hexadecimal_ = false;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STAGE_PIXEL_PROCESS_VIEW_H
