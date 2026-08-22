#ifndef PNGA_UI_QT_STAGE_PREVIEW_VIEW_H
#define PNGA_UI_QT_STAGE_PREVIEW_VIEW_H

// WP-5U3C: adaptive status view for the non-Image preview stages. It reports
// immutable stage availability and format conditions; decoding remains in the
// analysis engine and no full-size QImage is created here.

#include <pnga/analysis-engine/stage_analysis.h>

#include <QWidget>
#include <QString>

#include <cstdint>
#include <memory>

class QLabel;

namespace pnga::ui::qt {

enum class PreviewStage { kFilterMap, kFiltered, kDefiltered };

class StagePreviewView final : public QWidget {
  Q_OBJECT
 public:
  StagePreviewView(PreviewStage stage, QWidget* parent = nullptr);

  void setStageSet(
      std::shared_ptr<const pnga::analysis_engine::StageSet> stages);
  void clear();
  void setCoordinate(std::uint64_t x, std::uint64_t y);
  QString summary() const;

 private:
  void refresh();
  QString conditions() const;

  PreviewStage stage_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stages_;
  QLabel* label_ = nullptr;
  std::uint64_t x_ = 0;
  std::uint64_t y_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STAGE_PREVIEW_VIEW_H
