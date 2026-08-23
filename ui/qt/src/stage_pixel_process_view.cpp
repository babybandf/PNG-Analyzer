#include "pnga/ui/qt/stage_pixel_process_view.h"

#include <QFontDatabase>
#include <QSizePolicy>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace pnga::ui::qt {

namespace {

using pnga::analysis_engine::StagePixelProcessCalculation;
using pnga::analysis_engine::StagePixelProcessCell;
using pnga::analysis_engine::StagePixelProcessStage;
using ModelStagePixelProcessView =
    pnga::analysis_engine::StagePixelProcessView;

QString number(std::uint64_t value, bool hexadecimal) {
  return hexadecimal ? QStringLiteral("0x%1").arg(value, 0, 16)
                     : QString::number(value);
}

QString byte(std::uint8_t value, bool hexadecimal) {
  return number(value, hexadecimal);
}

QString stage_title(StagePixelProcessStage stage, bool hexadecimal) {
  switch (stage) {
    case StagePixelProcessStage::kNative:
      return QStringLiteral("Native pixels (%1)")
          .arg(hexadecimal ? QStringLiteral("HEX") : QStringLiteral("DEC"));
    case StagePixelProcessStage::kFiltered:
      return QStringLiteral("Filtered bytes (%1)")
          .arg(hexadecimal ? QStringLiteral("HEX") : QStringLiteral("DEC"));
    case StagePixelProcessStage::kDefiltered:
      return QStringLiteral("Defiltered bytes (%1)")
          .arg(hexadecimal ? QStringLiteral("HEX") : QStringLiteral("DEC"));
  }
  return {};
}

QString cell_value(const StagePixelProcessCell& cell, StagePixelProcessStage stage,
                   bool hexadecimal) {
  if (!cell.in_bounds) {
    return QStringLiteral("—");
  }
  if (stage == StagePixelProcessStage::kNative) {
    if (cell.native_samples.empty()) {
      return QStringLiteral("?");
    }
    return number(cell.native_samples.front(), hexadecimal);
  }
  if (cell.bytes.empty()) {
    return QStringLiteral("?");
  }
  if (cell.bytes.size() == 2) {
    return QStringLiteral("H:%1 L:%2")
        .arg(byte(cell.bytes[0], hexadecimal), byte(cell.bytes[1], hexadecimal));
  }
  return byte(cell.bytes.front(), hexadecimal);
}

QString filter_name(pnga::png_reconstruction::FilterType filter) {
  switch (filter) {
    case pnga::png_reconstruction::FilterType::kSub:
      return QStringLiteral("Sub");
    case pnga::png_reconstruction::FilterType::kUp:
      return QStringLiteral("Up");
    case pnga::png_reconstruction::FilterType::kAverage:
      return QStringLiteral("Average");
    case pnga::png_reconstruction::FilterType::kPaeth:
      return QStringLiteral("Paeth");
    case pnga::png_reconstruction::FilterType::kNone:
    default:
      return QStringLiteral("None");
  }
}

QString format_calculation(const StagePixelProcessCalculation& calculation,
                           bool hexadecimal) {
  QString text = QStringLiteral("%1 lane %2: X=%3")
                     .arg(calculation.channel_index)
                     .arg(calculation.lane)
                     .arg(byte(calculation.raw, hexadecimal));
  if (calculation.has_a) {
    text += QStringLiteral(" a=%1").arg(byte(calculation.a, hexadecimal));
  }
  if (calculation.has_b) {
    text += QStringLiteral(" b=%1").arg(byte(calculation.b, hexadecimal));
  }
  if (calculation.has_c) {
    text += QStringLiteral(" c=%1").arg(byte(calculation.c, hexadecimal));
  }
  if (calculation.has_predictor) {
    text += QStringLiteral(" predictor=%1")
                .arg(byte(calculation.predictor, hexadecimal));
  }
  if (calculation.has_recon) {
    text += QStringLiteral(" -> recon=%1")
                .arg(byte(calculation.recon, hexadecimal));
  }
  return text;
}

QString render_text(const ModelStagePixelProcessView& view,
               const pnga::analysis_engine::StageSet& stages,
               bool hexadecimal) {
  QString text = stage_title(view.stage, hexadecimal);
  text += QStringLiteral("\ncoordinate=(%1, %2)  color type=%3  bit depth=%4  channels=%5")
              .arg(view.image_x)
              .arg(view.image_y)
              .arg(stages.header.color_type)
              .arg(stages.header.bit_depth)
              .arg(view.channels.size());
  text += QStringLiteral("\npass=%1 local=(%2, %3) stream row=%4")
              .arg(view.pass_index)
              .arg(view.pass_local_x)
              .arg(view.pass_local_y)
              .arg(view.stream_row);
  if (view.stage != StagePixelProcessStage::kNative) {
    text += QStringLiteral("  filter=%1 (%2) at byte offset %3")
                .arg(view.filter_byte)
                .arg(filter_name(view.filter))
                .arg(view.filter_byte_offset);
  }
  text += QLatin1Char('\n');

  constexpr std::array<int, 5> kColumns{-2, -1, 0, 1, 2};
  for (const auto& channel : view.channels) {
    text += QStringLiteral("\n[%1]\n       ").arg(QString::fromStdString(channel.name));
    for (const int column : kColumns) {
      text += QStringLiteral("%1%2 ")
                  .arg(column == 0 ? QStringLiteral("*") : QStringLiteral(" "))
                  .arg(column, 3);
    }
    text += QLatin1Char('\n');
    for (int row = 0; row < 3; ++row) {
      text += QStringLiteral("%1 ").arg(row - 1, 3);
      for (int column = 0; column < 5; ++column) {
        const auto& cell = channel.cells[static_cast<std::size_t>(row * 5 + column)];
        QString value = cell_value(cell, view.stage, hexadecimal);
        if (cell.current) {
          value = QStringLiteral("CURRENT:%1").arg(value);
          if (view.stage == StagePixelProcessStage::kFiltered) {
            value = QStringLiteral("X/") + value;
          }
        } else if (view.stage == StagePixelProcessStage::kDefiltered &&
                   cell.in_bounds) {
          const bool has_a = view.filter == pnga::png_reconstruction::FilterType::kSub ||
                             view.filter == pnga::png_reconstruction::FilterType::kAverage ||
                             view.filter == pnga::png_reconstruction::FilterType::kPaeth;
          const bool has_b = view.filter == pnga::png_reconstruction::FilterType::kUp ||
                             view.filter == pnga::png_reconstruction::FilterType::kAverage ||
                             view.filter == pnga::png_reconstruction::FilterType::kPaeth;
          const bool has_c = view.filter == pnga::png_reconstruction::FilterType::kPaeth;
          if (row == 1 && column == 1 && has_a) {
            value = QStringLiteral("a/") + value;
          } else if (row == 0 && column == 2 && has_b) {
            value = QStringLiteral("b/") + value;
          } else if (row == 0 && column == 1 && has_c) {
            value = QStringLiteral("c/") + value;
          }
        }
        text += value.leftJustified(14, QLatin1Char(' '));
      }
      text += QLatin1Char('\n');
    }
  }

  text += QStringLiteral("\nCurrent value calculation\n");
  switch (view.stage) {
    case StagePixelProcessStage::kNative:
      text += QStringLiteral("Defiltered packed bytes -> native source sample; ")
              + (stages.header.interlace ? QStringLiteral("Adam7 pass mapping")
                                         : QStringLiteral("non-interlaced row mapping"));
      break;
    case StagePixelProcessStage::kFiltered:
      text += QStringLiteral("Inflate output -> filtered X; byte offset is relative to the scanline data after the filter byte.");
      break;
    case StagePixelProcessStage::kDefiltered:
      text += QStringLiteral("X + predictor (mod 256) -> reconstructed byte; actual dependencies only.\n");
      for (const auto& calculation : view.calculations) {
        text += format_calculation(calculation, hexadecimal) + QLatin1Char('\n');
      }
      break;
  }
  if (view.stage == StagePixelProcessStage::kFiltered) {
    text += QStringLiteral("\nPacked samples share a filtered byte where bit depth is below 8.");
  }
  return text;
}

}  // namespace

StagePixelProcessView::StagePixelProcessView(
    pnga::analysis_engine::StagePixelProcessStage stage, QWidget* parent)
    : QWidget(parent), stage_(stage) {
  const char* object_name = "stagePixelProcessView";
  if (stage == StagePixelProcessStage::kNative) {
    object_name = "pixelsViewport";
  } else if (stage == StagePixelProcessStage::kFiltered) {
    object_name = "filteredViewport";
  } else if (stage == StagePixelProcessStage::kDefiltered) {
    object_name = "defilteredViewport";
  }
  setObjectName(QString::fromLatin1(object_name));
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  text_ = new QTextEdit(this);
  text_->setObjectName(QStringLiteral("stagePixelProcessText"));
  text_->setReadOnly(true);
  text_->setAcceptRichText(false);
  text_->setLineWrapMode(QTextEdit::WidgetWidth);
  text_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  text_->setMinimumWidth(0);
  text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  layout->addWidget(text_);
  refresh();
}

void StagePixelProcessView::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages) {
  stages_ = std::move(stages);
  refresh();
}

void StagePixelProcessView::clear() {
  stages_.reset();
  refresh();
}

void StagePixelProcessView::setCoordinate(std::uint64_t x, std::uint64_t y) {
  x_ = x;
  y_ = y;
  refresh();
}

void StagePixelProcessView::setNumericBase(bool hexadecimal) {
  if (hexadecimal_ == hexadecimal) {
    return;
  }
  hexadecimal_ = hexadecimal;
  refresh();
}

void StagePixelProcessView::refresh() {
  if (text_ == nullptr) {
    return;
  }
  if (stages_ == nullptr || !stages_->success) {
    text_->setPlainText(stage_title(stage_, hexadecimal_) +
                        QStringLiteral("\nStage not available"));
    return;
  }
  const auto view = pnga::analysis_engine::build_stage_pixel_process_view(
      *stages_, stage_, x_, y_);
  if (view.status != pnga::analysis_engine::StagePixelProcessStatus::kReady) {
    text_->setPlainText(stage_title(stage_, hexadecimal_) + QStringLiteral("\n") +
                        QString::fromLatin1(
                            pnga::analysis_engine::stage_pixel_process_status_text(
                                view.status)));
    return;
  }
  text_->setPlainText(render_text(view, *stages_, hexadecimal_));
}

}  // namespace pnga::ui::qt
