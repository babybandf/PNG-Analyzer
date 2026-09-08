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
    case Qt::DecorationRole:
      if (const auto* image = thumbnails_.object(index.row())) return *image;
      emit thumbnailRequested(entry.ordinal);
      { QImage placeholder(96, 60, QImage::Format_RGBA8888);
        placeholder.fill(Qt::transparent); return placeholder; }
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
  thumbnails_.clear();
  beginResetModel();
  timeline_ = timeline;
  endResetModel();
}

void AnimationTimelineModel::clear() {
  thumbnails_.clear();
  beginResetModel();
  timeline_ = {};
  endResetModel();
}

void AnimationTimelineModel::setThumbnail(std::uint32_t ordinal, const QImage& image) {
  if (ordinal >= static_cast<std::uint32_t>(rowCount())) return;
  QImage thumbnail = image.scaled(96, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (thumbnail.isNull()) { thumbnail = QImage(96, 60, QImage::Format_RGBA8888); thumbnail.fill(Qt::gray); }
  thumbnails_.insert(static_cast<int>(ordinal), new QImage(thumbnail));
  const auto item = index(static_cast<int>(ordinal), 0);
  emit dataChanged(item, item, {Qt::DecorationRole});
}

}  // namespace pnga::ui::qt
