// WP-306 stage inspector model implementation. Reads the immutable StageSet and
// formats one value per channel at the current stage for the selected pixel.

#include "pnga/ui/qt/stage_inspector_model.h"

#include <pnga/png-reconstruction/reverse_filter.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include <QModelIndex>
#include <QVariant>

#include <cstdint>
#include <limits>

namespace pnga::ui::qt {

namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (b != 0 && a > std::numeric_limits<std::uint64_t>::max() / b) return false;
  out = a * b;
  return true;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (a > std::numeric_limits<std::uint64_t>::max() - b) return false;
  out = a + b;
  return true;
}

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
  if (offset > data.size() || count > data.size() - offset) {
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

std::optional<std::uint8_t> StageInspectorModel::deliveredChannel(
    std::uint64_t x, std::uint64_t y, std::uint8_t channel) const {
  if (delivered_rgba_.empty() || x >= delivered_width_ ||
      y >= delivered_height_ || channel >= 4) {
    return std::nullopt;
  }
  const std::uint64_t pixel = y * delivered_width_ + x;
  const std::uint64_t offset = pixel * 4 + channel;
  if (offset >= delivered_rgba_.size()) {
    return std::nullopt;
  }
  return u8(delivered_rgba_[offset]);
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
      std::uint64_t row = 0;
      if (!checked_add(cursor, (y - pass.y_start) / pass.y_step, row)) {
        return std::nullopt;
      }
      return row;
    }
      if (!checked_add(cursor, pass.height, cursor)) return std::nullopt;
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
      std::uint64_t idx = 0;
      std::uint64_t row = 0;
      if (!checked_mul(y_, w, row) || !checked_add(row, x_, row) ||
          !checked_mul(row, channels, idx) ||
          !checked_add(idx, channel, idx)) {
        return QStringLiteral("—");
      }
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
          std::uint64_t off = 0;
          std::uint64_t row_off = 0;
          std::uint64_t sample = 0;
          if (!checked_mul(y_, *row_bytes, row_off) ||
              !checked_mul(x_, channels, sample) ||
              !checked_add(sample, channel, sample) ||
              !checked_mul(sample, bps, sample) ||
              !checked_add(row_off, sample, off)) {
            return QStringLiteral("—");
          }
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
        std::uint64_t bit = 0;
        std::uint64_t row_samples = 0;
        std::uint64_t pixel_samples = 0;
        if (!checked_mul(y_, w, row_samples) ||
            !checked_mul(row_samples, channels, row_samples) ||
            !checked_mul(x_, channels, pixel_samples) ||
            !checked_add(row_samples, pixel_samples, row_samples) ||
            !checked_add(row_samples, channel, row_samples) ||
            !checked_mul(row_samples, bd, bit)) {
          return QStringLiteral("—");
        }
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
