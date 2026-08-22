#ifndef PNGA_UI_QT_PIXEL_VIEWPORT_H
#define PNGA_UI_QT_PIXEL_VIEWPORT_H

// WP-5U3B: a small native-sample viewport consumer. It asks the Qt-free
// analysis engine for a bounded window and never materializes a full image.

#include <pnga/analysis-engine/stage_viewport.h>

#include <QWidget>

#include <cstdint>
#include <memory>

class QLabel;

namespace pnga::ui::qt {

class PixelViewport final : public QWidget {
  Q_OBJECT
 public:
  explicit PixelViewport(QWidget* parent = nullptr);

  void setStageSet(
      std::shared_ptr<const pnga::analysis_engine::StageSet> stages);
  void clear();
  void setCenter(std::uint64_t x, std::uint64_t y);

 private:
  void refresh();

  pnga::analysis_engine::StageViewportProvider provider_;
  QLabel* label_ = nullptr;
  std::uint64_t center_x_ = 0;
  std::uint64_t center_y_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_PIXEL_VIEWPORT_H
