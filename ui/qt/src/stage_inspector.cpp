// WP-5U7 reconstruction report. Formatting is kept in Qt; all reconstruction
// facts come from the immutable analysis-engine view model.

#include "pnga/ui/qt/stage_inspector.h"

#include <pnga/analysis-engine/reconstruct_view_model.h>
#include <pnga/png-reconstruction/reverse_filter.h>
#include <pnga/ui/qt/application_theme.h>

#include <QTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

namespace pnga::ui::qt {

namespace {

constexpr qreal kContentDocumentMargin = 8.0;

QString esc(const QString& value) { return value.toHtmlEscaped(); }

QString number(std::uint64_t value, bool hex) {
  return hex ? QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16)
             : QString::number(static_cast<qulonglong>(value));
}

QString value8(std::uint8_t value, bool hex) { return number(value, hex); }

QString row_coordinate(std::uint64_t origin, int delta, bool hexadecimal) {
  const auto magnitude = static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
  if (delta < 0) {
    if (origin < magnitude) {
      return QStringLiteral("—");
    }
    return number(origin - magnitude, hexadecimal);
  }
  if (origin > std::numeric_limits<std::uint64_t>::max() - magnitude) {
    return QStringLiteral("—");
  }
  return number(origin + magnitude, hexadecimal);
}

QString section(const QString& title) {
  return QStringLiteral("<h3 style=\"margin:10px 0 3px 0;\">%1</h3>")
      .arg(esc(title));
}

struct SourceKey {
  std::int64_t x = 0;
  std::int64_t y = 0;
  bool operator<(const SourceKey& other) const noexcept {
    return y != other.y ? y < other.y : x < other.x;
  }
};

struct RoleSet {
  bool a = false;
  bool b = false;
  bool c = false;
};

QString role_text(const RoleSet& roles) {
  QStringList result;
  if (roles.a) result.push_back(QStringLiteral("a"));
  if (roles.b) result.push_back(QStringLiteral("b"));
  if (roles.c) result.push_back(QStringLiteral("c"));
  return result.join(QStringLiteral(","));
}

QString cell_style(const QPalette& palette, bool current,
                   const RoleSet& roles) {
  const bool dark = palette.color(QPalette::Base).lightness() < 128;
  const bool dependency = !current && (roles.a || roles.b || roles.c);
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
  QColor text = ApplicationTheme::applicationColor(
      ApplicationTheme::ColorToken::kText);
  if (!text.isValid()) {
    text = dark ? QColor("#f5f5f5") : QColor("#202124");
  }
  return QStringLiteral("background:%1;color:%2;border:1px solid %3;padding:3px;min-width:76px;text-align:center;")
      .arg(background.name(QColor::HexRgb), text.name(QColor::HexRgb),
           border.name(QColor::HexRgb));
}

}  // namespace

StageInspector::StageInspector(QWidget* parent) : QWidget(parent) {
  model_ = new StageInspectorModel(this);
  report_ = new QTextEdit(this);
  report_->setObjectName(QStringLiteral("reconstructReport"));
  report_->setAccessibleName(QStringLiteral("Reconstruction report"));
  report_->setReadOnly(true);
  report_->setAcceptRichText(true);
  report_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                   Qt::TextSelectableByKeyboard);
  report_->setFont(ApplicationTheme::applicationMonospaceFont());
  report_->document()->setDocumentMargin(kContentDocumentMargin);
  report_->setPlaceholderText(QStringLiteral("Select a pixel to inspect"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(report_, 1);
  refreshReport();
}

void StageInspector::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> set) {
  model_->setStageSet(std::move(set));
  refreshReport();
}

void StageInspector::setDeliveredPixels(std::uint32_t width,
                                        std::uint32_t height,
                                        std::vector<std::byte> rgba) {
  model_->setDeliveredPixels(width, height, std::move(rgba));
  refreshReport();
}

void StageInspector::setNumericBase(bool hexadecimal) {
  if (hexadecimal_ == hexadecimal) return;
  hexadecimal_ = hexadecimal;
  refreshReport();
}

void StageInspector::setRowQueryStatus(const QString& status_text) {
  query_status_ = status_text;
  refreshReport();
}

void StageInspector::clear() {
  model_->clear();
  query_status_ = QStringLiteral("not indexed");
  refreshReport();
}

void StageInspector::onPixelSelected(std::uint64_t x, std::uint64_t y) {
  x_ = x;
  y_ = y;
  model_->setPixel(x, y);
  refreshReport();
}

void StageInspector::refreshReport() {
  QString html = QStringLiteral("<div style=\"white-space:pre-wrap;\">");
  if (!model_->hasData()) {
    html += QStringLiteral("<h3>Reconstruction</h3><p>Empty — no image analysis is available.</p></div>");
    report_->setHtml(html);
    return;
  }
  const auto& stages = *model_->stageSet();
  const auto reconstruction = pnga::analysis_engine::build_reconstruct_view(
      stages, x_, y_);
  const auto format_name = [&]() {
    const char* name = "Unknown";
    switch (stages.header.color_type) {
      case 0: name = "Gray"; break;
      case 2: name = "RGB"; break;
      case 3: name = "Indexed"; break;
      case 4: name = "Gray + Alpha"; break;
      case 6: name = "RGBA"; break;
      default: break;
    }
    return QStringLiteral("%1 %2-bit, %3")
        .arg(QLatin1String(name))
        .arg(stages.header.bit_depth)
        .arg(stages.interlace ? QStringLiteral("Adam7")
                              : QStringLiteral("non-interlaced"));
  };

  html += section(QStringLiteral("Target pixel"));
  const auto effective_channels =
      pnga::png_reconstruction::channels_for_color_type(stages.header.color_type);
  html += QStringLiteral("<p>coordinate: (%1, %2)<br>PNG format: %3<br>effective source channels: %4</p>")
      .arg(number(x_, hexadecimal_), number(y_, hexadecimal_),
           esc(format_name()), number(effective_channels, hexadecimal_));
  if (reconstruction.status !=
      pnga::analysis_engine::ReconstructStatus::kReady) {
    const bool out_of_bounds = reconstruction.status ==
                               pnga::analysis_engine::ReconstructStatus::kOutOfRange;
    html += QStringLiteral("<p><b>%1</b>: %2</p><p>Scanline materialization: %3</p></div>")
        .arg(out_of_bounds ? QStringLiteral("Out of bounds")
                           : QStringLiteral("Unsupported reconstruction"),
             esc(QString::fromStdString(reconstruction.error)), esc(query_status_));
    report_->setHtml(html);
    return;
  }

  html += QStringLiteral("<p>pass: %1 (%2, %3)</p>")
      .arg(number(reconstruction.pass, hexadecimal_),
           number(reconstruction.pass_x, hexadecimal_),
           number(reconstruction.pass_y, hexadecimal_));
  html += section(QStringLiteral("Scanline location"));
  html += QStringLiteral("<p>stream row: %1, sample index: %2<br>filtered byte offset: %3<br>unfiltered byte offset: %4</p>")
      .arg(number(reconstruction.stream_row, hexadecimal_),
           number(reconstruction.sample_index, hexadecimal_),
           number(reconstruction.filtered_byte_offset, hexadecimal_),
           number(reconstruction.unfiltered_byte_offset, hexadecimal_));

  const auto formula = pnga::analysis_engine::filter_formula(
      stages, reconstruction.stream_row);
  if (!formula.success || reconstruction.selected_byte >= formula.events.size()) {
    html += QStringLiteral("<p><b>Unsupported reconstruction</b>: filter trace unavailable.</p></div>");
    report_->setHtml(html);
    return;
  }
  const auto filter = formula.filter;
  const auto selected = formula.events[reconstruction.selected_byte];
  const auto channels = effective_channels;
  const auto channel_name = [&](std::uint8_t channel) {
    if (stages.header.color_type == 0) return QStringLiteral("Gray");
    if (stages.header.color_type == 3) return QStringLiteral("Index");
    if (stages.header.color_type == 4)
      return channel == 0 ? QStringLiteral("Gray") : QStringLiteral("Alpha");
    const QStringList names{QStringLiteral("R"), QStringLiteral("G"),
                            QStringLiteral("B"), QStringLiteral("A")};
    return names.at(channel);
  };
  html += section(QStringLiteral("Filtered data"));
  const QString filter_label = QStringLiteral("%1 (%2)")
                                   .arg(QLatin1String(
                                       pnga::png_reconstruction::filter_type_text(
                                           filter)))
                                   .arg(static_cast<int>(filter));
  const QString filter_chip =
      QStringLiteral("<span style=\"background-color:#FFF4CC;color:#4A3B00;"
                     "padding:2px 5px;font-weight:bold;\">%1</span>")
          .arg(esc(filter_label));
  const auto filtered_x_for_channel = [&](std::uint8_t channel) {
    const std::uint64_t bytes_per_sample =
        stages.header.bit_depth >= 8 ? stages.header.bit_depth / 8 : 1;
    if (bytes_per_sample == 0 ||
        channel > std::numeric_limits<std::uint64_t>::max() /
                       bytes_per_sample) {
      return QStringLiteral("—");
    }
    const auto channel_offset =
        static_cast<std::uint64_t>(channel) * bytes_per_sample;
    if (reconstruction.selected_byte >
        std::numeric_limits<std::uint64_t>::max() - channel_offset) {
      return QStringLiteral("—");
    }
    const auto begin = reconstruction.selected_byte + channel_offset;
    if (begin >= formula.events.size() ||
        bytes_per_sample > formula.events.size() - begin) {
      return QStringLiteral("—");
    }
    QStringList values;
    for (std::uint64_t byte = 0; byte < bytes_per_sample; ++byte) {
      values.push_back(value8(
          formula.events[begin + byte].raw, hexadecimal_));
    }
    return values.join(QStringLiteral(" "));
  };
  QStringList filtered_x_values;
  for (std::uint8_t channel = 0; channel < channels; ++channel) {
    filtered_x_values.push_back(
        QStringLiteral("%1=%2")
            .arg(channel_name(channel), filtered_x_for_channel(channel)));
  }
  html += QStringLiteral(
      "<p>filter: %1, selected source byte: %2<br>raw filtered X: %3</p>")
      .arg(filter_chip)
      .arg(number(reconstruction.selected_byte, hexadecimal_))
      .arg(filtered_x_values.join(QStringLiteral(", ")));

  // For byte-addressable non-interlaced images map a/b/c back to logical
  // pixels. Packed, indexed, 16-bit and Adam7 data stays source-byte-only.
  const bool logical_highlight = !stages.interlace && stages.header.bit_depth == 8 &&
                                 stages.header.color_type != 3 && channels > 0;
  std::map<SourceKey, RoleSet> dependencies;
  if (logical_highlight) {
    const auto bpp = *pnga::png_reconstruction::filter_bpp(
        stages.header.bit_depth, stages.header.color_type);
    const auto source_for = [&](std::uint8_t role, std::uint64_t byte)
        -> std::optional<SourceKey> {
      if ((role == 0 || role == 2) && reconstruction.selected_byte < bpp) {
        return std::nullopt;
      }
      if ((role == 1 || role == 2) && reconstruction.pass_y == 0) {
        return std::nullopt;
      }
      const std::int64_t byte_delta = static_cast<std::int64_t>(byte) -
                                      static_cast<std::int64_t>(reconstruction.selected_byte);
      const std::int64_t pixel_delta = byte_delta / static_cast<std::int64_t>(channels);
      const std::int64_t sx = static_cast<std::int64_t>(x_) + pixel_delta;
      const std::int64_t sy = static_cast<std::int64_t>(y_) +
                              (role == 1 || role == 2 ? -1 : 0);
      if (sx < 0 || sy < 0 || sx >= stages.header.width || sy >= stages.header.height)
        return std::nullopt;
      return SourceKey{sx, sy};
    };
    const auto add = [&](std::uint8_t role, std::uint64_t byte) {
      const auto key = source_for(role, byte);
      if (!key.has_value()) return;
      if (role == 0) dependencies[*key].a = true;
      if (role == 1) dependencies[*key].b = true;
      if (role == 2) dependencies[*key].c = true;
    };
    const auto previous_byte = reconstruction.selected_byte >= bpp
                                   ? reconstruction.selected_byte - bpp
                                   : reconstruction.selected_byte;
    switch (filter) {
      case pnga::png_reconstruction::FilterType::kSub:
        add(0, previous_byte);
        break;
      case pnga::png_reconstruction::FilterType::kUp:
        add(1, reconstruction.selected_byte);
        break;
      case pnga::png_reconstruction::FilterType::kAverage:
        add(0, previous_byte);
        add(1, reconstruction.selected_byte);
        break;
      case pnga::png_reconstruction::FilterType::kPaeth:
        add(0, previous_byte);
        add(1, reconstruction.selected_byte);
        add(2, previous_byte);
        break;
      case pnga::png_reconstruction::FilterType::kNone:
        break;
    }
  }

  html += section(QStringLiteral("Pixel neighborhood"));
  for (std::uint8_t channel = 0; channel < channels; ++channel) {
    html += QStringLiteral("<h4>%1</h4><table cellspacing=\"2\"><tr><th></th>")
        .arg(channel_name(channel));
    for (int dx = -2; dx <= 2; ++dx) {
      const std::int64_t column = static_cast<std::int64_t>(x_) + dx;
      html += QStringLiteral("<th>%1</th>").arg(
          column < 0 ? QStringLiteral("—")
                     : number(static_cast<std::uint64_t>(column), hexadecimal_));
    }
    html += QStringLiteral("</tr>");
    for (int dy = -1; dy <= 1; ++dy) {
      html += QStringLiteral("<tr><th>%1</th>")
          .arg(row_coordinate(y_, dy, hexadecimal_));
      for (int dx = -2; dx <= 2; ++dx) {
        const std::int64_t sx = static_cast<std::int64_t>(x_) + dx;
        const std::int64_t sy = static_cast<std::int64_t>(y_) + dy;
        const bool current = dx == 0 && dy == 0;
        QString sample = QStringLiteral("—");
        if (sx >= 0 && sy >= 0 && sx < stages.header.width &&
            sy < stages.header.height) {
          const auto sy64 = static_cast<std::uint64_t>(sy);
          const auto sx64 = static_cast<std::uint64_t>(sx);
          if (stages.header.width != 0 &&
              sy64 <= std::numeric_limits<std::uint64_t>::max() /
                             stages.header.width) {
            const auto row_base = sy64 * stages.header.width;
            if (sx64 <= std::numeric_limits<std::uint64_t>::max() - row_base) {
              const auto pixel = row_base + sx64;
              if (channels != 0 &&
                  pixel <= std::numeric_limits<std::uint64_t>::max() /
                                 channels) {
                const auto base = pixel * channels;
                if (base <= std::numeric_limits<std::uint64_t>::max() - channel &&
                    base + channel < stages.native.samples.size()) {
                  sample = number(stages.native.samples[base + channel], hexadecimal_);
                }
              }
            }
          }
        }
        if (current) {
          sample = filtered_x_for_channel(channel);
        }
        const auto found = dependencies.find(SourceKey{sx, sy});
        const QString role = found == dependencies.end() ? QString() : role_text(found->second);
        const QString marker = current ? QStringLiteral("current") : role;
        const QString marker_html = marker.isEmpty()
                                        ? QString()
                                        : QStringLiteral(
                                              "<br><span style=\"font-size:small;font-weight:bold;\">%1</span>")
                                              .arg(esc(marker));
        html += QStringLiteral("<td style=\"%1\">%2%3</td>")
            .arg(cell_style(palette(), current,
                            found == dependencies.end() ? RoleSet{} : found->second),
                 esc(sample), marker_html);
      }
      html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
  }
  html += QStringLiteral("<p>Legend: current; a/b/c = filter dependencies.");
  if (!logical_highlight) {
    html += QStringLiteral("<br>Source-byte dependence is shown; logical-pixel highlighting is unavailable for packed, indexed, 16-bit, or Adam7 data.");
  }
  html += QStringLiteral("</p>");

  html += section(QStringLiteral("Filter / predictor / bounds"));
  html += QStringLiteral("<p>filter: %1; a=%2, b=%3, c=%4; boundary neighbors are zero.<br>scanline materialization: %5</p>")
      .arg(QLatin1String(pnga::png_reconstruction::filter_type_text(filter)),
           value8(selected.a, hexadecimal_), value8(selected.b, hexadecimal_),
           value8(selected.c, hexadecimal_), esc(query_status_));
  html += section(QStringLiteral("Per-channel reconstruction"));
  if (stages.header.bit_depth == 8 && stages.header.color_type != 3) {
    for (std::uint8_t channel = 0; channel < channels; ++channel) {
      if (channel > std::numeric_limits<std::uint64_t>::max() -
                         reconstruction.selected_byte) {
        break;
      }
      const auto offset = reconstruction.selected_byte + channel;
      if (offset >= formula.events.size()) break;
      const auto& event = formula.events[offset];
      QString predictor_formula;
      QString predictor_values;
      switch (filter) {
        case pnga::png_reconstruction::FilterType::kNone:
          predictor_formula = QStringLiteral("0");
          predictor_values = QStringLiteral("0 = %1")
                                 .arg(value8(event.predictor, hexadecimal_));
          break;
        case pnga::png_reconstruction::FilterType::kSub:
          predictor_formula = QStringLiteral("a");
          predictor_values = QStringLiteral("%1 = %2")
                                 .arg(value8(event.a, hexadecimal_),
                                      value8(event.predictor, hexadecimal_));
          break;
        case pnga::png_reconstruction::FilterType::kUp:
          predictor_formula = QStringLiteral("b");
          predictor_values = QStringLiteral("%1 = %2")
                                 .arg(value8(event.b, hexadecimal_),
                                      value8(event.predictor, hexadecimal_));
          break;
        case pnga::png_reconstruction::FilterType::kAverage:
          predictor_formula = QStringLiteral("floor((a + b) / 2)");
          predictor_values = QStringLiteral("floor((%1 + %2) / 2) = %3")
                                 .arg(value8(event.a, hexadecimal_),
                                      value8(event.b, hexadecimal_),
                                      value8(event.predictor, hexadecimal_));
          break;
        case pnga::png_reconstruction::FilterType::kPaeth:
          predictor_formula = QStringLiteral("Paeth(a, b, c)");
          predictor_values = QStringLiteral("Paeth(%1, %2, %3) = %4")
                                 .arg(value8(event.a, hexadecimal_),
                                      value8(event.b, hexadecimal_),
                                      value8(event.c, hexadecimal_),
                                      value8(event.predictor, hexadecimal_));
          break;
      }
      html += QStringLiteral("<h4>%1 (channel %2)</h4>")
                  .arg(esc(channel_name(channel)),
                       number(channel, hexadecimal_));
      html += QStringLiteral(
          "<p><b>Neighbor pixels</b>: a (left)=%1, b (up)=%2, "
          "c (upleft)=%3</p>")
                  .arg(value8(event.a, hexadecimal_),
                       value8(event.b, hexadecimal_),
                       value8(event.c, hexadecimal_));
      html += QStringLiteral(
          "<p><b>Predictor formula</b>: %1<br>"
          "<b>Substituted values</b>: %2</p>")
                  .arg(esc(predictor_formula), predictor_values);
      html += QStringLiteral(
          "<p><b>Filter calculation</b>: recon = (X + predictor) mod 256<br>"
          "<b>Substituted values</b>: (%1 + %2) mod 256 = %3</p>")
                  .arg(value8(event.raw, hexadecimal_),
                       value8(event.predictor, hexadecimal_),
                       value8(event.recon, hexadecimal_));
    }
  } else {
    html += QStringLiteral("<p>Source-byte reconstruction is available for this sample; logical per-channel mapping is unsupported for packed, indexed, 16-bit, or Adam7 data.</p>");
  }
  html += section(QStringLiteral("Final RGBA"));
  QStringList rgba;
  for (std::uint8_t channel = 0; channel < 4; ++channel) {
    const auto value = model_->deliveredChannel(x_, y_, channel);
    if (value.has_value()) {
      rgba.push_back(value8(*value, hexadecimal_));
      continue;
    }
    std::uint64_t native_index = 0;
    const bool native_index_ok =
        y_ <= std::numeric_limits<std::uint64_t>::max() /
                         static_cast<std::uint64_t>(stages.header.width) &&
        (native_index = y_ * stages.header.width,
         x_ <= std::numeric_limits<std::uint64_t>::max() - native_index) &&
        (native_index += x_,
         channels == 0 || native_index <=
                              std::numeric_limits<std::uint64_t>::max() /
                                  static_cast<std::uint64_t>(channels)) &&
        (channels == 0 || (native_index *= channels, true));
    std::optional<std::uint16_t> native;
    if (native_index_ok && native_index < stages.native.samples.size()) {
      if (stages.header.color_type == 6 && channel < 4) {
        if (native_index + channel < stages.native.samples.size())
          native = stages.native.samples[native_index + channel];
      } else if (stages.header.color_type == 2) {
        native = channel < 3 && native_index + channel < stages.native.samples.size()
                     ? std::optional<std::uint16_t>(stages.native.samples[native_index + channel])
                     : std::optional<std::uint16_t>(channel < 3 ? 0 : 255);
      } else if (stages.header.color_type == 0) {
        native = channel < 3 ? stages.native.samples[native_index]
                             : std::optional<std::uint16_t>(255);
      } else if (stages.header.color_type == 4) {
        native = channel < 3 && native_index < stages.native.samples.size()
                     ? stages.native.samples[native_index]
                     : native_index + 1 < stages.native.samples.size()
                           ? std::optional<std::uint16_t>(stages.native.samples[native_index + 1])
                           : std::optional<std::uint16_t>(0);
      }
    }
    rgba.push_back(native.has_value() ? number(*native, hexadecimal_)
                                      : QStringLiteral("n/a"));
  }
  html += QStringLiteral("<p>RGBA(%1, %2, %3, %4)</p></div>")
      .arg(rgba.at(0), rgba.at(1), rgba.at(2), rgba.at(3));
  report_->setHtml(html);
}

}  // namespace pnga::ui::qt
