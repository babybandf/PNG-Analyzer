// WP-5U3C adaptive stage tab status presentation.

#include "pnga/ui/qt/stage_preview_view.h"

#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>

namespace pnga::ui::qt {

StagePreviewView::StagePreviewView(PreviewStage stage, QWidget* parent)
    : QWidget(parent), stage_(stage) {
  auto* layout = new QVBoxLayout(this);
  label_ = new QLabel(this);
  label_->setObjectName(QStringLiteral("stageSummary"));
  label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(label_);
  refresh();
}

void StagePreviewView::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages) {
  stages_ = std::move(stages);
  refresh();
}

void StagePreviewView::clear() {
  stages_.reset();
  refresh();
}

void StagePreviewView::setCoordinate(std::uint64_t x, std::uint64_t y) {
  x_ = x;
  y_ = y;
  refresh();
}

QString StagePreviewView::conditions() const {
  if (stages_ == nullptr) {
    return {};
  }
  const auto& header = stages_->header;
  QStringList flags;
  if (header.interlace) {
    flags.push_back(QStringLiteral("Adam7"));
  }
  if (header.color_type == 3) {
    flags.push_back(QStringLiteral("Palette index"));
  }
  if (header.color_type == 4 || header.color_type == 6) {
    flags.push_back(QStringLiteral("Alpha"));
  }
  if (header.bit_depth < 8) {
    flags.push_back(QStringLiteral("Packed samples"));
  }
  if (header.bit_depth == 16) {
    flags.push_back(QStringLiteral("16-bit samples"));
  }
  return flags.isEmpty() ? QStringLiteral("None") : flags.join(QStringLiteral(", "));
}

QString StagePreviewView::summary() const {
  if (stages_ == nullptr || !stages_->success) {
    return QStringLiteral("Not available for current document");
  }
  QString text;
  switch (stage_) {
    case PreviewStage::kFilterMap:
      text = QStringLiteral("Filter Map\nscanlines: %1")
                 .arg(static_cast<qulonglong>(stages_->scanlines.size()));
      break;
    case PreviewStage::kFiltered:
      text = QStringLiteral("Filtered bytes\nbytes: %1\nscanlines: %2")
                 .arg(static_cast<qulonglong>(stages_->filtered.size()))
                 .arg(static_cast<qulonglong>(stages_->scanlines.size()));
      break;
    case PreviewStage::kDefiltered:
      text = QStringLiteral("Unfiltered bytes\nbytes: %1")
                 .arg(static_cast<qulonglong>(stages_->unfiltered.size()));
      break;
  }
  text += QStringLiteral("\ncoordinate: (%1, %2)\nconditions: %3")
              .arg(static_cast<qulonglong>(x_))
              .arg(static_cast<qulonglong>(y_))
              .arg(conditions());
  if (stages_->interlace || stages_->header.interlace) {
    text += QStringLiteral("\nrows are stored pass-major (Adam7)");
  }
  return text;
}

void StagePreviewView::refresh() { label_->setText(summary()); }

}  // namespace pnga::ui::qt
