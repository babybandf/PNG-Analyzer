#ifndef PNGA_UI_QT_STAGE_INSPECTOR_MODEL_H
#define PNGA_UI_QT_STAGE_INSPECTOR_MODEL_H

// WP-306: stage inspector table model. Rows are the channels of the selected
// pixel; columns are [channel, value-at-current-stage]. Switching stage at a
// fixed (x, y) keeps the row meaning — coordinate consistency across stages.
// The data comes from the immutable analysis-engine StageSet; this widget never
// runs the pipeline (AGENTS.md).

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/trace-model/selection.h>

#include <QAbstractTableModel>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace pnga::ui::qt {

class StageInspectorModel final : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { kChannel = 0, kValue = 1, kColumnCount = 2 };

  explicit StageInspectorModel(QObject* parent = nullptr);

  // Owns nothing: `set` is shared and must stay alive while the model is used.
  void setStageSet(std::shared_ptr<const pnga::analysis_engine::StageSet> set);
  void clear();

  void setStage(pnga::trace_model::Stage stage);
  void setPixel(std::uint64_t x, std::uint64_t y);

  // Optional delivered RGBA8 buffer (width*height*4) so the Delivered stage can
  // show a value; without it that stage reads as "n/a".
  void setDeliveredPixels(std::uint32_t width, std::uint32_t height,
                          std::vector<std::byte> rgba);

  bool hasData() const noexcept {
    return set_ != nullptr && set_->success;
  }
  pnga::trace_model::Stage stage() const noexcept { return stage_; }
  std::uint64_t pixelX() const noexcept { return x_; }
  std::uint64_t pixelY() const noexcept { return y_; }

  // Filter formula text for the selected byte at `byte_index` within the
  // pixel's scanline, e.g. "row 5 byte 7: Paeth a=.. b=.. c=.. pred=.. recon=..".
  std::optional<QString> formulaText(std::uint64_t byte_index) const;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation = Qt::Horizontal,
                      int role = Qt::DisplayRole) const override;

 private:
  QString valueAt(std::uint64_t channel) const;
  // Stream-order row of the scanline containing image pixel (x, y).
  std::optional<std::uint64_t> streamRowForPixel(
      std::uint64_t x, std::uint64_t y, std::uint64_t* pass_x) const;

  std::shared_ptr<const pnga::analysis_engine::StageSet> set_;
  std::vector<std::byte> delivered_rgba_;
  std::uint32_t delivered_width_ = 0;
  std::uint32_t delivered_height_ = 0;
  std::uint64_t x_ = 0;
  std::uint64_t y_ = 0;
  pnga::trace_model::Stage stage_ = pnga::trace_model::Stage::kFiltered;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_STAGE_INSPECTOR_MODEL_H
