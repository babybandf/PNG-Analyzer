// WP-306 stage inspector widget implementation. Thin Qt UI over the
// StageInspectorModel: stage scrubber, per-pixel value table and formula line.

#include "pnga/ui/qt/stage_inspector.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QModelIndex>
#include <QTableView>
#include <QVBoxLayout>

namespace pnga::ui::qt {

StageInspector::StageInspector(QWidget* parent) : QWidget(parent) {
  model_ = new StageInspectorModel(this);

  stage_combo_ = new QComboBox(this);
  // Order matches Stage::kFiltered -> kDelivered; values are the enum ints.
  stage_combo_->addItem(QStringLiteral("Filtered"),
                        static_cast<int>(pnga::trace_model::Stage::kFiltered));
  stage_combo_->addItem(QStringLiteral("Unfiltered"),
                        static_cast<int>(pnga::trace_model::Stage::kUnfiltered));
  stage_combo_->addItem(QStringLiteral("Native"),
                        static_cast<int>(pnga::trace_model::Stage::kNative));
  stage_combo_->addItem(QStringLiteral("Delivered"),
                        static_cast<int>(pnga::trace_model::Stage::kDelivered));
  stage_combo_->setEnabled(false);

  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->horizontalHeader()->setStretchLastSection(true);

  detail_ = new QLabel(QStringLiteral("no data"), this);
  detail_->setWordWrap(true);

  query_status_label_ = new QLabel(QStringLiteral("row query: indexed"), this);

  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel(QStringLiteral("Stage:"), this));
  top->addWidget(stage_combo_);
  top->addStretch();

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(table_, 1);
  layout->addWidget(detail_);
  layout->addWidget(query_status_label_);

  connect(stage_combo_, &QComboBox::currentIndexChanged, this,
          &StageInspector::onStageChanged);
  connect(table_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &StageInspector::onCurrentCellChanged);
}

void StageInspector::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> set) {
  model_->setStageSet(std::move(set));
  stage_combo_->setEnabled(model_->hasData());
  if (model_->hasData()) {
    stage_combo_->setCurrentIndex(0);  // Filtered
  }
  refreshDetail();
}

void StageInspector::setDeliveredPixels(std::uint32_t width,
                                        std::uint32_t height,
                                        std::vector<std::byte> rgba) {
  model_->setDeliveredPixels(width, height, std::move(rgba));
}

void StageInspector::setRowQueryStatus(const QString& status_text) {
  query_status_label_->setText(
      QStringLiteral("row query: %1").arg(status_text));
}

void StageInspector::clear() {
  model_->clear();
  stage_combo_->setEnabled(false);
  detail_->setText(QStringLiteral("no data"));
  query_status_label_->setText(QStringLiteral("row query: indexed"));
}

void StageInspector::onPixelSelected(std::uint64_t x, std::uint64_t y) {
  x_ = x;
  y_ = y;
  model_->setPixel(x, y);
  refreshDetail();
}

void StageInspector::onStageChanged(int index) {
  const auto stage = static_cast<pnga::trace_model::Stage>(
      stage_combo_->itemData(index).toInt());
  model_->setStage(stage);
  refreshDetail();
  emit stageChanged(stage);
}

void StageInspector::onCurrentCellChanged(const QModelIndex& current,
                                          const QModelIndex& /*previous*/) {
  if (current.isValid()) {
    refreshDetail();
  }
}

void StageInspector::refreshDetail() {
  if (!model_->hasData()) {
    detail_->setText(QStringLiteral("no data"));
    return;
  }
  // Show the formula for the pixel's first byte at the current stage (byte 0).
  const auto text = model_->formulaText(0);
  detail_->setText(text.value_or(
      QStringLiteral("pixel (%1, %2) — token provenance not indexed yet")
          .arg(static_cast<qulonglong>(x_))
          .arg(static_cast<qulonglong>(y_))));
}

}  // namespace pnga::ui::qt
