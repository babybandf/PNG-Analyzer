#include "pnga/ui/qt/animation_timeline_model.h"

namespace pnga::ui::qt {

AnimationTimelineModel::AnimationTimelineModel(QObject* parent)
    : QAbstractListModel(parent) {}

int AnimationTimelineModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(timeline_.entries.size());
}

QVariant AnimationTimelineModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      static_cast<std::size_t>(index.row()) >= timeline_.entries.size()) {
    return {};
  }
  const auto& entry = timeline_.entries[static_cast<std::size_t>(index.row())];
  switch (role) {
    case Qt::DisplayRole:
      return QStringLiteral("Frame %1").arg(entry.ordinal);
    case OrdinalRole:
      return entry.ordinal;
    case RawNumeratorRole:
      return entry.raw_num;
    case RawDenominatorRole:
      return entry.raw_den;
    case StartNanosecondsRole:
      return QVariant::fromValue<qulonglong>(entry.start_ns);
    case DurationNanosecondsRole:
      return QVariant::fromValue<qulonglong>(entry.duration_ns);
    default:
      return {};
  }
}

QHash<int, QByteArray> AnimationTimelineModel::roleNames() const {
  return {{OrdinalRole, "ordinal"},
          {RawNumeratorRole, "rawNumerator"},
          {RawDenominatorRole, "rawDenominator"},
          {StartNanosecondsRole, "startNanoseconds"},
          {DurationNanosecondsRole, "durationNanoseconds"}};
}

void AnimationTimelineModel::setTimeline(
    const pnga::analysis_engine::AnimationTimeline& timeline) {
  beginResetModel();
  timeline_ = timeline;
  endResetModel();
}

void AnimationTimelineModel::clear() {
  beginResetModel();
  timeline_ = {};
  endResetModel();
}

}  // namespace pnga::ui::qt
