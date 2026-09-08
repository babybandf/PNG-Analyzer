#ifndef PNGA_UI_QT_ANIMATION_INSPECTOR_H
#define PNGA_UI_QT_ANIMATION_INSPECTOR_H

#include <pnga/png-format/animation_index.h>

#include <QWidget>

class QLabel;

namespace pnga::ui::qt {

class AnimationInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit AnimationInspector(QWidget* parent = nullptr);

  void setFrameControl(const pnga::png_format::FrameControl& control);
  QString summaryText() const;
  void setPlaybackContext(double speed, std::uint32_t loops);

 private:
  QLabel* summary_ = nullptr;
  QString summary_text_;
  pnga::png_format::FrameControl control_{};
  double speed_ = 1;
  std::uint32_t loops_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_ANIMATION_INSPECTOR_H
