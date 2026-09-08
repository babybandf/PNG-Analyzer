#include <pnga/ui/qt/animation_inspector.h>
#include <pnga/ui/qt/animation_timeline.h>
#include <pnga/ui/qt/animation_timeline_model.h>

#include <pnga/analysis-engine/animation_playback.h>

#include <QtTest/QtTest>

using pnga::analysis_engine::AnimationTimeline;
using pnga::analysis_engine::TimelineEntry;
using pnga::ui::qt::AnimationInspector;
using pnga::ui::qt::AnimationTimelineModel;
using pnga::ui::qt::AnimationTimelineWidget;

class AnimationUiTest final : public QObject {
  Q_OBJECT

 private slots:
  void largeTimelineStaysInTheModel();
  void timelineEmitsInteractionSignals();
  void inspectorShowsControlMetadata();
};

void AnimationUiTest::largeTimelineStaysInTheModel() {
  AnimationTimeline timeline;
  timeline.complete = true;
  timeline.entries.reserve(100000);
  for (std::uint32_t i = 0; i < 100000; ++i) {
    timeline.entries.push_back(TimelineEntry{i, 1, 100, i * 10000000ull,
                                              10000000ull});
  }
  AnimationTimelineModel model;
  model.setTimeline(timeline);
  QCOMPARE(model.rowCount(), 100000);
  QCOMPARE(model.data(model.index(99999, 0),
                      AnimationTimelineModel::OrdinalRole)
               .toUInt(),
           99999u);
}

void AnimationUiTest::timelineEmitsInteractionSignals() {
  AnimationTimelineWidget widget;
  QSignalSpy frame(&widget, &AnimationTimelineWidget::frameRequested);
  QSignalSpy play(&widget, &AnimationTimelineWidget::playRequested);
  QSignalSpy speed(&widget, &AnimationTimelineWidget::speedRequested);
  widget.requestFrameForTesting(7);
  widget.requestPlayForTesting();
  widget.requestSpeedForTesting(2);
  QCOMPARE(frame.count(), 1);
  QCOMPARE(frame.at(0).at(0).toUInt(), 7u);
  QCOMPARE(play.count(), 1);
  QCOMPARE(speed.count(), 1);
  QCOMPARE(speed.at(0).at(0).toInt(), 2);
}

void AnimationUiTest::inspectorShowsControlMetadata() {
  AnimationInspector inspector;
  inspector.setFrameControl(
      pnga::png_format::FrameControl{9, 10, 11, 2, 3, 4, 5, 1, 0});
  QVERIFY(inspector.summaryText().contains(QStringLiteral("10×11")));
  QVERIFY(inspector.summaryText().contains(QStringLiteral("sequence 9")));
}

QTEST_MAIN(AnimationUiTest)
#include "animation_ui_test.moc"

