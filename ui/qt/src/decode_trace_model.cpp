// WP-5U12E Decode Trace model implementation: lazy formatting of immutable
// projection facts. Event text, typed ranges and Match arithmetic are
// rendered exactly as the Qt-free projection produced them; Qt never decodes
// or re-derives facts.

#include "pnga/ui/qt/decode_trace_model.h"

#include <pnga/ui/qt/application_theme.h>

#include <QBrush>

namespace pnga::ui::qt {

namespace {

// Domain-labelled display values. A single-position range collapses to one
// number; an empty range renders as an em dash. Plain decimal digits keep
// the output locale-independent.
QString span_text(std::uint64_t begin, std::uint64_t end) {
  if (end <= begin) {
    return QStringLiteral("—");
  }
  if (end - begin == 1) {
    return QString::number(static_cast<qulonglong>(begin));
  }
  return QStringLiteral("%1–%2")
      .arg(static_cast<qulonglong>(begin))
      .arg(static_cast<qulonglong>(end));
}

QString bits_text(const pnga::trace_model::DeflateBitRange& range) {
  if (!range.valid() || range.empty()) {
    return QStringLiteral("—");
  }
  return QStringLiteral("%1–%2")
      .arg(static_cast<qulonglong>(range.begin.value))
      .arg(static_cast<qulonglong>(range.end.value));
}

QString bytes_text(const pnga::trace_model::InflatedByteRange& range) {
  if (!range.valid() || range.empty()) {
    return QStringLiteral("—");
  }
  return span_text(range.begin.value, range.end.value);
}

QString accessible_span(std::uint64_t begin, std::uint64_t end) {
  if (end <= begin) {
    return QStringLiteral("no output");
  }
  if (end - begin == 1) {
    return QStringLiteral("byte %1").arg(static_cast<qulonglong>(begin));
  }
  return QStringLiteral("bytes %1 to %2")
      .arg(static_cast<qulonglong>(begin))
      .arg(static_cast<qulonglong>(end));
}

QString accessible_bits(const pnga::trace_model::DeflateBitRange& range) {
  if (!range.valid() || range.empty()) {
    return QStringLiteral("no input bits");
  }
  return QStringLiteral("bits %1 to %2")
      .arg(static_cast<qulonglong>(range.begin.value))
      .arg(static_cast<qulonglong>(range.end.value));
}

}  // namespace

DecodeTraceModel::DecodeTraceModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void DecodeTraceModel::setView(
    std::shared_ptr<
        const pnga::analysis_engine::DecodeTraceInspectorView>
        view) {
  beginResetModel();
  view_ = std::move(view);
  endResetModel();
}

void DecodeTraceModel::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  state_ = state;
  if (rowCount() > 0 && columnCount() > 0) {
    const QModelIndex top = index(0, 0);
    const QModelIndex bottom = index(rowCount() - 1, ColumnCount - 1);
    emit dataChanged(top, bottom,
                     {Qt::DisplayRole, Qt::BackgroundRole,
                      DecodeTraceContainsCurrentRole,
                      DecodeTraceIsManualSelectionRole,
                      DecodeTraceAccessibleTextRole});
  }
}

const pnga::analysis_engine::DecodeTraceStep* DecodeTraceModel::stepAt(
    int row) const noexcept {
  if (view_ == nullptr || row < 0 ||
      row >= static_cast<int>(view_->steps.size())) {
    return nullptr;
  }
  return &view_->steps[static_cast<std::size_t>(row)];
}

int DecodeTraceModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || view_ == nullptr) {
    return 0;
  }
  return static_cast<int>(view_->steps.size());
}

int DecodeTraceModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return ColumnCount;
}

bool DecodeTraceModel::containsCurrent(
    const pnga::analysis_engine::DecodeTraceStep& step) const noexcept {
  if (view_ != nullptr && state_.generation == view_->scope.generation &&
      state_.current.has_value() &&
      state_.current->output_range.valid() &&
      state_.current->output_range.overlaps(step.output_range)) {
    return true;
  }
  return step.contains_current;
}

bool DecodeTraceModel::isManualSelection(
    const pnga::analysis_engine::DecodeTraceStep& step) const noexcept {
  if (step.selected) {
    return true;
  }
  if (view_ != nullptr && state_.generation == view_->scope.generation &&
      state_.manual.has_value() &&
      state_.manual->token_index.has_value()) {
    return *state_.manual->token_index == step.token_index;
  }
  return false;
}

QVariant DecodeTraceModel::data(const QModelIndex& index, int role) const {
  const auto* step = stepAt(index.row());
  if (step == nullptr || index.column() < 0 ||
      index.column() >= ColumnCount) {
    return {};
  }
  const bool contains_current = containsCurrent(*step);
  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case Current:
          return contains_current ? QStringLiteral("●") : QString();
        case Step:
          return QString::number(static_cast<qulonglong>(step->token_index));
        case InputBits:
          return bits_text(step->input_range);
        case Event:
          return QString::fromStdString(step->event_text);
        case Output:
          return bytes_text(step->output_range);
        default:
          return {};
      }
    case Qt::FontRole:
      // Numeric/offset cells use the shared monospace token (flow-ui §14).
      switch (index.column()) {
        case Step:
        case InputBits:
        case Output:
          return ApplicationTheme::applicationMonospaceFont();
        default:
          return {};
      }
    case Qt::BackgroundRole:
      // Centralized theme token; never a hard-coded RGB literal. The Current
      // marker stays visually distinct from the native row selection
      // (flow-ui §20.6).
      if (contains_current) {
        const QColor current = ApplicationTheme::applicationColor(
            ApplicationTheme::ColorToken::kCurrentPixel);
        if (current.isValid()) {
          return QBrush(current);
        }
      }
      return {};
    case DecodeTraceStepRole:
      return QVariant::fromValue(*step);
    case DecodeTraceContainsCurrentRole:
      return contains_current;
    case DecodeTraceIsManualSelectionRole:
      return isManualSelection(*step);
    case DecodeTraceAccessibleTextRole: {
      QString text =
          QStringLiteral("Step %1, DEFLATE %2, %3, inflated output %4")
              .arg(static_cast<qulonglong>(step->token_index))
              .arg(accessible_bits(step->input_range))
              .arg(QString::fromStdString(step->event_text))
              .arg(accessible_span(step->output_range.begin.value,
                                   step->output_range.end.value));
      if (contains_current) {
        text += QStringLiteral(", contains current output");
      }
      if (isManualSelection(*step)) {
        text += QStringLiteral(", manually selected");
      }
      return text;
    }
    default:
      return {};
  }
}

QVariant DecodeTraceModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  switch (section) {
    case Current:
      return QStringLiteral("Current");
    case Step:
      return QStringLiteral("Step");
    case InputBits:
      return QStringLiteral("Input bits");
    case Event:
      return QStringLiteral("Event");
    case Output:
      return QStringLiteral("Output");
    default:
      return {};
  }
}

}  // namespace pnga::ui::qt
