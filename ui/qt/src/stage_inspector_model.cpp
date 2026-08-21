// WP-306 stage inspector model implementation. Reads the immutable StageSet and
// formats one value per channel at the current stage for the selected pixel.

#include "pnga/ui/qt/stage_inspector_model.h"

#include <pnga/png-reconstruction/reverse_filter.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <QModelIndex>
#include <QVariant>

#include <cstdint>

namespace pnga::ui::qt {

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

// Big-endian bit read for sub-byte samples (same order as the pipeline).
std::uint8_t read_bits(const std::byte* data, std::uint64_t bit_pos,
                       unsigned bits) {
  std::uint8_t v = 0;
  for (unsigned b = 0; b < bits; ++b) {
    const std::uint64_t pos = bit_pos + b;
    const unsigned shift = 7 - static_cast<unsigned>(pos % 8);
    v = static_cast<std::uint8_t>(
        (v << 1) | ((static_cast<unsigned>(data[pos / 8]) >> shift) & 1u));
  }
  return v;
}

QString hex_bytes(const std::vector<std::byte>& data, std::uint64_t offset,
                  std::size_t count) {
  if (offset + count > data.size()) {
    return QStringLiteral("—");
  }
  QString out;
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) {
      out += QLatin1Char(' ');
    }
    out += QStringLiteral("0x%1").arg(
        static_cast<unsigned>(u8(data[offset + i])), 2, 16, QLatin1Char('0'));
  }
  return out;
}

}  // namespace

StageInspectorModel::StageInspectorModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void StageInspectorModel::setStageSet(
    std::shared_ptr<const pnga::analysis_engine::StageSet> set) {
  beginResetModel();
  set_ = std::move(set);
  x_ = 0;
  y_ = 0;
  stage_ = pnga::trace_model::Stage::kFiltered;
  endResetModel();
}

void StageInspectorModel::clear() {
  beginResetModel();
  set_.reset();
  delivered_rgba_.clear();
  delivered_width_ = 0;
  delivered_height_ = 0;
  endResetModel();
}

void StageInspectorModel::setStage(pnga::trace_model::Stage stage) {
  if (stage_ == stage) {
    return;
  }
  beginResetModel();
  stage_ = stage;
  endResetModel();
}

void StageInspectorModel::setPixel(std::uint64_t x, std::uint64_t y) {
  if (x_ == x && y_ == y) {
    return;
  }
  beginResetModel();
  x_ = x;
  y_ = y;
  endResetModel();
}

void StageInspectorModel::setDeliveredPixels(std::uint32_t width,
                                             std::uint32_t height,
                                             std::vector<std::byte> rgba) {
  beginResetModel();
  delivered_width_ = width;
  delivered_height_ = height;
  delivered_rgba_ = std::move(rgba);
  endResetModel();
}

std::optional<std::uint64_t> StageInspectorModel::streamRowForPixel(
    std::uint64_t x, std::uint64_t y, std::uint64_t* pass_x) const {
  if (!hasData()) {
    return std::nullopt;
  }
  const auto layout =
      pnga::png_reconstruction::compute_scanline_layout(set_->header);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  std::uint64_t cursor = 0;
  for (std::size_t p = 0; p < layout->pass_count; ++p) {
    const auto& pass = layout->passes[p];
    if (pass.height == 0) {
      continue;
    }
    if (x >= pass.x_start && (x - pass.x_start) % pass.x_step == 0 &&
        y >= pass.y_start && (y - pass.y_start) % pass.y_step == 0) {
      if (pass_x != nullptr) {
        *pass_x = (x - pass.x_start) / pass.x_step;
      }
      return cursor + (y - pass.y_start) / pass.y_step;
    }
    cursor += pass.height;
  }
  return std::nullopt;
}

QString StageInspectorModel::valueAt(std::uint64_t channel) const {
  if (!hasData()) {
    return QStringLiteral("—");
  }
  const auto& set = *set_;
  const std::uint64_t w = set.header.width;
  const std::uint64_t h = set.header.height;
  if (x_ >= w || y_ >= h) {
    return QStringLiteral("—");
  }
  const std::uint64_t channels = set.native.channels;
  if (channel >= channels) {
    return QStringLiteral("—");
  }
  const std::uint8_t bd = set.header.bit_depth;

  switch (stage_) {
    case pnga::trace_model::Stage::kNative: {
      const std::uint64_t idx = (y_ * w + x_) * channels + channel;
      if (idx >= set.native.samples.size()) {
        return QStringLiteral("—");
      }
      return QString::number(set.native.samples[idx]);
    }
    case pnga::trace_model::Stage::kFiltered:
    case pnga::trace_model::Stage::kUnfiltered: {
      const auto row_bytes = pnga::png_reconstruction::row_bytes(
          static_cast<std::uint32_t>(w), bd, set.header.color_type);
      if (!row_bytes.has_value()) {
        return QStringLiteral("—");
      }
      if (bd >= 8) {
        const unsigned bps = bd / 8;
        if (stage_ == pnga::trace_model::Stage::kUnfiltered) {
          const std::uint64_t off =
              y_ * *row_bytes + (x_ * channels + channel) * bps;
          return hex_bytes(set.unfiltered, off, bps);
        }
        std::uint64_t pass_x = 0;
        const auto stream_row = streamRowForPixel(x_, y_, &pass_x);
        if (!stream_row.has_value()) {
          return QStringLiteral("—");
        }
        const auto& span = set.scanlines[*stream_row];
        const std::uint64_t off =
            span.offset + 1 + (pass_x * channels + channel) * bps;
        return hex_bytes(set.filtered, off, bps);
      }
      if (stage_ == pnga::trace_model::Stage::kUnfiltered) {
        const std::uint64_t bit =
            (y_ * w * channels + x_ * channels + channel) * bd;
        return QStringLiteral("0x%1").arg(
            read_bits(set.unfiltered.data(), bit, bd), 1, 16, QLatin1Char('0'));
      }
      std::uint64_t pass_x = 0;
      const auto stream_row = streamRowForPixel(x_, y_, &pass_x);
      if (!stream_row.has_value()) {
        return QStringLiteral("—");
      }
      const auto& span = set.scanlines[*stream_row];
      const std::uint64_t bit =
          (pass_x * channels + channel) * bd;
      const std::byte* row = set.filtered.data() + span.offset + 1;
      return QStringLiteral("0x%1").arg(
          read_bits(row, bit, bd), 1, 16, QLatin1Char('0'));
    }
    case pnga::trace_model::Stage::kDelivered: {
      if (delivered_rgba_.empty() || x_ >= delivered_width_ ||
          y_ >= delivered_height_ || channel >= 4) {
        return QStringLiteral("n/a");
      }
      const std::uint64_t idx =
          (y_ * delivered_width_ + x_) * 4 + channel;
      if (idx >= delivered_rgba_.size()) {
        return QStringLiteral("n/a");
      }
      return QString::number(u8(delivered_rgba_[idx]));
    }
    default:
      return QStringLiteral("—");
  }
}

std::optional<QString> StageInspectorModel::formulaText(
    std::uint64_t byte_index) const {
  if (!hasData()) {
    return std::nullopt;
  }
  std::uint64_t pass_x = 0;
  const auto stream_row = streamRowForPixel(x_, y_, &pass_x);
  if (!stream_row.has_value()) {
    return std::nullopt;
  }
  const auto formula =
      pnga::analysis_engine::filter_formula(*set_, *stream_row);
  if (!formula.success || byte_index >= formula.events.size()) {
    return std::nullopt;
  }
  const auto& ev = formula.events[byte_index];
  return QStringLiteral("row %1 byte %2: %3  a=%4 b=%5 c=%6 pred=%7 recon=%8")
      .arg(static_cast<qulonglong>(*stream_row))
      .arg(static_cast<qulonglong>(byte_index))
      .arg(QLatin1String(
          pnga::png_reconstruction::filter_type_text(formula.filter)))
      .arg(ev.a)
      .arg(ev.b)
      .arg(ev.c)
      .arg(ev.predictor)
      .arg(ev.recon);
}

int StageInspectorModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0
                          : (hasData() ? static_cast<int>(set_->native.channels)
                                       : 0);
}

int StageInspectorModel::columnCount(const QModelIndex& /*parent*/) const {
  return kColumnCount;
}

QVariant StageInspectorModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || !hasData()) {
    return {};
  }
  if (role == Qt::DisplayRole) {
    if (index.column() == kChannel) {
      return QString::number(index.row());
    }
    return valueAt(static_cast<std::uint64_t>(index.row()));
  }
  return {};
}

QVariant StageInspectorModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
    return {};
  }
  if (section == kChannel) {
    return QStringLiteral("Channel");
  }
  return QStringLiteral("Value (%1)")
      .arg(QLatin1String(pnga::trace_model::stage_text(stage_)));
}

}  // namespace pnga::ui::qt
