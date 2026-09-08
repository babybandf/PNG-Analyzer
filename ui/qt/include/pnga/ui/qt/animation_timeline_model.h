#ifndef PNGA_UI_QT_ANIMATION_TIMELINE_MODEL_H
#define PNGA_UI_QT_ANIMATION_TIMELINE_MODEL_H

#include <pnga/analysis-engine/animation_playback.h>

#include <QAbstractListModel>

namespace pnga::ui::qt {

class AnimationTimelineModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role {
    OrdinalRole = Qt::UserRole + 1,
    RawNumeratorRole,
    RawDenominatorRole,
    StartNanosecondsRole,
    DurationNanosecondsRole,
  };

  explicit AnimationTimelineModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setTimeline(const pnga::analysis_engine::AnimationTimeline& timeline);
  void clear();

 private:
  pnga::analysis_engine::AnimationTimeline timeline_;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_ANIMATION_TIMELINE_MODEL_H
