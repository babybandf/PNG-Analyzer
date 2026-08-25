#include "pnga/ui/qt/stage_pixel_process_view.h"

#include <pnga/ui/qt/application_theme.h>
#include <QColor>
#include <QPalette>
#include <QSizePolicy>
#include <QTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>

namespace pnga::ui::qt {

namespace {

constexpr qreal kContentDocumentMargin = 8.0;

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
      return QStringLiteral("Unfiltered bytes (%1)")
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

QString esc(const QString& value) { return value.toHtmlEscaped(); }

QString cell_style(const QPalette& palette, bool current, bool dependency) {
  const bool dark = palette.color(QPalette::Base).lightness() < 128;
  const auto token = current
                         ? ApplicationTheme::ColorToken::kCurrentPixel
                         : dependency ? ApplicationTheme::ColorToken::kDependency
                                      : ApplicationTheme::ColorToken::kNeutral;
  QColor background = ApplicationTheme::applicationColor(token);
  if (!background.isValid()) {
    background = dark ? QColor("#173f53") : QColor("#f4f7f9");
  }
  QColor border = ApplicationTheme::applicationColor(
      current ? ApplicationTheme::ColorToken::kAccent
              : ApplicationTheme::ColorToken::kBorder);
  if (!border.isValid()) {
    border = QColor("#aab7c2");
  }
  QColor foreground = ApplicationTheme::applicationColor(
      ApplicationTheme::ColorToken::kText);
  if (!foreground.isValid()) {
    foreground = dark ? QColor("#f5f5f5") : QColor("#202124");
  }
  return QStringLiteral(
             "background:%1;color:%2;border:1px solid %3;padding:3px;"
             "min-width:76px;text-align:center;white-space:nowrap;")
      .arg(background.name(QColor::HexRgb), foreground.name(QColor::HexRgb),
           border.name(QColor::HexRgb));
}

QString dependency_marker(const ModelStagePixelProcessView& view, int row,
                          int column) {
  if (view.stage != StagePixelProcessStage::kDefiltered) {
    return {};
  }
  const bool has_a = view.filter == pnga::png_reconstruction::FilterType::kSub ||
                     view.filter == pnga::png_reconstruction::FilterType::kAverage ||
                     view.filter == pnga::png_reconstruction::FilterType::kPaeth;
  const bool has_b = view.filter == pnga::png_reconstruction::FilterType::kUp ||
                     view.filter == pnga::png_reconstruction::FilterType::kAverage ||
                     view.filter == pnga::png_reconstruction::FilterType::kPaeth;
  const bool has_c = view.filter == pnga::png_reconstruction::FilterType::kPaeth;
  if (row == 1 && column == 1 && has_a) return QStringLiteral("a");
  if (row == 0 && column == 2 && has_b) return QStringLiteral("b");
  if (row == 0 && column == 1 && has_c) return QStringLiteral("c");
  return {};
}

QString row_coordinate(const ModelStagePixelProcessView& view, int delta,
                       bool hexadecimal) {
  const auto magnitude = static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
  if (delta < 0) {
    if (view.image_y < magnitude) {
      return QStringLiteral("—");
    }
    return number(view.image_y - magnitude, hexadecimal);
  }
  if (view.image_y > std::numeric_limits<std::uint64_t>::max() - magnitude) {
    return QStringLiteral("—");
  }
  return number(view.image_y + magnitude, hexadecimal);
}

QString render_html(const ModelStagePixelProcessView& view,
                    const pnga::analysis_engine::StageSet& stages,
                    bool hexadecimal, const QPalette& palette) {
  QString html = QStringLiteral("<div style=\"white-space:pre-wrap;\">");
  html += QStringLiteral("<h3>%1</h3>")
              .arg(esc(stage_title(view.stage, hexadecimal)));
  html += QStringLiteral(
              "<p>coordinate=(%1, %2)<br>color type=%3, bit depth=%4, "
              "channels=%5<br>pass=%6, local=(%7, %8), stream row=%9</p>")
              .arg(view.image_x)
              .arg(view.image_y)
              .arg(stages.header.color_type)
              .arg(stages.header.bit_depth)
              .arg(view.channels.size())
              .arg(view.pass_index)
              .arg(view.pass_local_x)
              .arg(view.pass_local_y)
              .arg(view.stream_row);
  if (view.stage != StagePixelProcessStage::kNative) {
    html += QStringLiteral("<p>filter=%1 (%2), filter byte offset=%3</p>")
                .arg(view.filter_byte)
                .arg(esc(filter_name(view.filter)))
                .arg(view.filter_byte_offset);
  }

  for (const auto& channel : view.channels) {
    html += QStringLiteral("<h4>%1</h4><table cellspacing=\"2\"><tr><th></th>")
                .arg(esc(QString::fromStdString(channel.name)));
    for (int column = -2; column <= 2; ++column) {
      const auto coordinate = static_cast<std::int64_t>(view.image_x) + column;
      html += QStringLiteral("<th>%1</th>")
                  .arg(coordinate < 0
                           ? QStringLiteral("—")
                           : number(static_cast<std::uint64_t>(coordinate),
                                    hexadecimal));
    }
    html += QStringLiteral("</tr>");
    for (int row = 0; row < 3; ++row) {
      const int dy = row - 1;
      html += QStringLiteral("<tr><th>%1</th>")
                  .arg(row_coordinate(view, dy, hexadecimal));
      for (int column = 0; column < 5; ++column) {
        const auto& cell = channel.cells[static_cast<std::size_t>(row * 5 + column)];
        const QString marker = cell.current
                                   ? QStringLiteral("current")
                                   : (cell.in_bounds
                                          ? dependency_marker(view, row, column)
                                          : QString());
        QString value = esc(cell_value(cell, view.stage, hexadecimal));
        if (!marker.isEmpty()) {
          value += QStringLiteral("<br><b>%1</b>").arg(esc(marker));
        }
        html += QStringLiteral("<td style=\"%1\">%2</td>")
                    .arg(cell_style(palette, cell.current,
                                    !cell.current && !marker.isEmpty()),
                         value);
      }
      html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
  }

  html += QStringLiteral("<h4>Current value calculation</h4>");
  switch (view.stage) {
    case StagePixelProcessStage::kNative:
      html += QStringLiteral(
          "<p>Unfiltered packed bytes → native source sample; %1 mapping.</p>")
                  .arg(stages.header.interlace ? QStringLiteral("Adam7 pass")
                                                : QStringLiteral("non-interlaced row"));
      break;
    case StagePixelProcessStage::kFiltered:
      html += QStringLiteral(
          "<p>Inflate output → filtered <b>X</b>; byte offset is relative to "
          "the scanline data after the filter byte.</p>");
      break;
    case StagePixelProcessStage::kDefiltered:
      html += QStringLiteral(
          "<p><b>X</b> + predictor (mod 256) → reconstructed byte; actual "
          "dependencies only.</p><ul>");
      for (const auto& calculation : view.calculations) {
        html += QStringLiteral("<li>%1</li>")
                    .arg(esc(format_calculation(calculation, hexadecimal)));
      }
      html += QStringLiteral("</ul>");
      break;
  }
  if (view.stage == StagePixelProcessStage::kFiltered) {
    html += QStringLiteral(
        "<p>Packed samples share a filtered byte where bit depth is below 8.</p>");
  }
  html += QStringLiteral(
      "<p>Legend: <b>current</b> is the selected pixel; <b>a/b/c</b> are "
      "filter dependencies. Other colored cells are reference pixels.</p></div>");
  return html;
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
  text_->setAcceptRichText(true);
  text_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                 Qt::TextSelectableByKeyboard);
  text_->setLineWrapMode(QTextEdit::WidgetWidth);
  text_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_->setFont(ApplicationTheme::applicationMonospaceFont());
  text_->document()->setDocumentMargin(kContentDocumentMargin);
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
    text_->setHtml(QStringLiteral("<h3>%1</h3><p>Stage not available</p>")
                       .arg(esc(stage_title(stage_, hexadecimal_))));
    return;
  }
  const auto view = pnga::analysis_engine::build_stage_pixel_process_view(
      *stages_, stage_, x_, y_);
  if (view.status != pnga::analysis_engine::StagePixelProcessStatus::kReady) {
    text_->setHtml(QStringLiteral("<h3>%1</h3><p>%2</p>")
                       .arg(esc(stage_title(stage_, hexadecimal_)),
                            esc(QString::fromLatin1(
                                pnga::analysis_engine::stage_pixel_process_status_text(
                                    view.status)))));
    return;
  }
  text_->setHtml(render_html(view, *stages_, hexadecimal_, palette()));
}

}  // namespace pnga::ui::qt
