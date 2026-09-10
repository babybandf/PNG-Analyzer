// WP-APNG-INSPECT T07: FrameInspectionSession budgets, scheduling and
// background lifecycle (contract C5). All completions run through an
// injected deterministic executor — no sleeps; QSignalSpy captures every
// publication and the whole test runs under ctest's timeout as the
// deadlock guard.

#include "frame_inspection_session.h"

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/trace-model/inspection_context.h>

#include <QtTest/QtTest>

#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include "apng_inspection_fixture.h"

using pnga::analysis_engine::AnalysisTarget;
using pnga::analysis_engine::FrameStageSet;
using pnga::analysis_engine::JobPriority;
using pnga::gui::FrameInspectionSession;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::InspectionTicket;
using pnga::trace_model::PublicationScope;

Q_DECLARE_METATYPE(std::shared_ptr<const AnalysisTarget>)
Q_DECLARE_METATYPE(std::shared_ptr<const FrameStageSet>)
Q_DECLARE_METATYPE(InspectionTicket)

namespace {

struct ManualExecutor {
  // Queued jobs run only when the test invokes them, in submission order.
  std::deque<std::function<void()>> jobs;

  std::function<void(std::function<void()>)> dispatcher() {
    return [this](std::function<void()> job) { jobs.push_back(std::move(job)); };
  }
  std::size_t run_one() {
    if (jobs.empty()) {
      return 0;
    }
    auto job = std::move(jobs.front());
    jobs.pop_front();
    job();
    return 1;
  }
  std::size_t run_all() {
    std::size_t count = 0;
    while (!jobs.empty()) {
      count += run_one();
    }
    return count;
  }
};

InspectionTicket frame_ticket(std::uint32_t index, std::uint64_t epoch = 1,
                              std::uint64_t serial = 1) {
  return InspectionTicket{{7, AnimationFrame{index}},
                          pnga::trace_model::Stage::kPreBlend, epoch,
                          serial};
}

std::shared_ptr<const AnalysisTarget> built_target(std::uint32_t ordinal = 0) {
  auto request = pnga_test::inspection_request(ordinal);
  return pnga::analysis_engine::make_frame_target(request).target;
}

}  // namespace

class FrameInspectionSessionTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<std::shared_ptr<const AnalysisTarget>>();
    qRegisterMetaType<std::shared_ptr<const FrameStageSet>>();
    qRegisterMetaType<InspectionTicket>("InspectionTicket");
  }

  void gatesPublicationsByTicketScope() {
    FrameInspectionSession session;
    auto target = built_target();
    QVERIFY(target != nullptr);
    const InspectionTicket current = frame_ticket(0);

    QSignalSpy selected(&session, &FrameInspectionSession::targetSelected);
    session.selectTarget(target, current);
    QCOMPARE(selected.count(), 1);

    // Target scope ignores the selection serial (encoded data reuse); pixel
    // scope requires the exact stage and serial.
    auto same_epoch = current;
    ++same_epoch.selection_serial;
    QVERIFY(session.accepts(same_epoch, PublicationScope::kTarget));
    QVERIFY(!session.accepts(same_epoch, PublicationScope::kPixel));

    auto other_epoch = current;
    ++other_epoch.target_epoch;
    QVERIFY(!session.accepts(other_epoch, PublicationScope::kTarget));

    auto other_identity = current;
    other_identity.key.identity = AnimationFrame{1};
    QVERIFY(!session.accepts(other_identity, PublicationScope::kTarget));

    session.clear(8);
    QVERIFY(!session.accepts(current, PublicationScope::kTarget));
  }

  void openFrameBuildsAndPublishesWithoutPriorTarget() {
    // WP-APNG-INSPECT live wiring: openFrame is the app's entry point and
    // must build the FIRST target without any prior selectTarget call.
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());
    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);
    QSignalSpy failed(&session, &FrameInspectionSession::frameAnalysisFailed);

    auto request = pnga_test::inspection_request(0);
    session.openFrame(request, frame_ticket(0, 1));
    executor.run_all();
    QCOMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 0);
    QVERIFY(session.retainedBytes() > 0);
    auto frame =
        ready.back().at(0).value<std::shared_ptr<const FrameStageSet>>();
    QVERIFY(frame != nullptr);
    QVERIFY(frame->stages.success);
    QCOMPARE(frame->delivered.width, 2u);
    QCOMPARE(frame->delivered.height, 3u);
  }

  void lateCompletionsOfSupersededTargetsNeverPublish() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target_a = built_target(0);
    auto target_b = built_target(1);
    QVERIFY(target_a != nullptr);
    QVERIFY(target_b != nullptr);

    QSignalSpy cancelled(&session, &FrameInspectionSession::requestCancelled);
    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);
    QSignalSpy failed(&session, &FrameInspectionSession::frameAnalysisFailed);

    // Target sequence A1 -> B -> A2 over the same frame identity: the
    // target epoch increments on every switch (C1), so A1's late result
    // arrives under a superseded epoch and must be dropped by the
    // publication gate while A2 publishes.
    session.selectTarget(target_a, frame_ticket(0, 1, 3));
    session.requestFrameAnalysis(frame_ticket(0, 1, 3));
    session.selectTarget(target_b, frame_ticket(0, 2, 4));
    session.selectTarget(target_a, frame_ticket(0, 3, 5));
    session.requestFrameAnalysis(frame_ticket(0, 3, 5));

    QCOMPARE(executor.jobs.size(), 2);
    executor.run_all();
    QCOMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(ready.back().at(1).value<InspectionTicket>().selection_serial, 5u);
    QVERIFY(cancelled.count() >= 1);
  }

  void reservationsAreReleasedOnEveryExitPath() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target = built_target();
    QVERIFY(target != nullptr);
    session.selectTarget(target, frame_ticket(0));

    QSignalSpy cancelled(&session, &FrameInspectionSession::requestCancelled);
    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);

    session.requestFrameAnalysis(frame_ticket(0));
    QCOMPARE(session.reservedBytes(),
             FrameInspectionSession::kFrameAnalysisReservation);
    executor.run_all();
    QCOMPARE(session.reservedBytes(), 0u);
    QCOMPARE(ready.count(), 1);

    // A job whose single reservation exceeds the whole in-flight budget is
    // rejected up front and never runs.
    session.requestFrameAnalysis(frame_ticket(0), JobPriority::kSelection,
                                 FrameInspectionSession::kReservationBudget + 1);
    QCOMPARE(session.reservedBytes(), 0u);
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(executor.jobs.empty());
  }

  void clearingTheSessionDropsQueuedWorkWithoutPublishing() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target = built_target();
    QVERIFY(target != nullptr);
    session.selectTarget(target, frame_ticket(0));

    QSignalSpy cancelled(&session, &FrameInspectionSession::requestCancelled);
    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);

    session.requestFrameAnalysis(frame_ticket(0));
    executor.run_one();
    QCOMPARE(ready.count(), 1);
    session.requestFrameAnalysis(frame_ticket(0));
    session.clear(9);
    QCOMPARE(session.retainedBytes(), 0u);
    QCOMPARE(session.reservedBytes(), 0u);
    executor.run_all();
    QCOMPARE(ready.count(), 1);  // no publication after close
    QVERIFY(cancelled.count() >= 1);
  }

  void fullQueueDropsLowestPriorityAndKeepsSelection() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target = built_target();
    QVERIFY(target != nullptr);
    session.selectTarget(target, frame_ticket(0));

    QSignalSpy cancelled(&session, &FrameInspectionSession::requestCancelled);

    // Fill the queue with background statistics requests (8 cap) using
    // distinct frame keys so the duplicate merge does not collapse them.
    pnga::statistics::DocumentIdentity identity{
        1, "fnv1a64-v1:0123456789abcdef"};
    for (std::uint32_t i = 0; i < 9; ++i) {
      session.requestFrameStatistics(frame_ticket(i), identity,
                                     JobPriority::kBackground);
    }
    // One job occupies the single execution worker, eight fill the queue.
    QCOMPARE(session.reservedBytes(),
             9 * FrameInspectionSession::kFrameStatisticsReservation);
    QCOMPARE(cancelled.count(), 0);

    // A selection-priority request displaces the oldest background job.
    session.requestFrameAnalysis(frame_ticket(0), JobPriority::kSelection);
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(session.reservedBytes(),
             7 * FrameInspectionSession::kFrameStatisticsReservation +
                 FrameInspectionSession::kFrameAnalysisReservation +
                 FrameInspectionSession::kFrameStatisticsReservation);

    // A background request with a full queue of higher priority drops
    // itself: user selections are never displaced by background work.
    session.requestFrameStatistics(frame_ticket(0), identity,
                                   JobPriority::kBackground);
    QCOMPARE(cancelled.count(), 2);
  }

  void duplicateFrameRequestsMergeIntoNewest() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target = built_target();
    QVERIFY(target != nullptr);
    session.selectTarget(target, frame_ticket(0, 1));

    QSignalSpy cancelled(&session, &FrameInspectionSession::requestCancelled);
    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);
    // The first request occupies the single execution worker; two more for
    // the same frame merge, cancelling the middle one.
    session.requestFrameAnalysis(frame_ticket(0, 1, 1));
    session.requestFrameAnalysis(frame_ticket(0, 1, 2));
    session.requestFrameAnalysis(frame_ticket(0, 1, 3));
    QCOMPARE(executor.jobs.size(), 1);
    QCOMPARE(cancelled.count(), 1);
    executor.run_all();
    // The in-flight first request completes, then the newest merged request
    // runs; the cancelled middle one never publishes.
    QCOMPARE(ready.count(), 2);
    QCOMPARE(ready.back().at(1).value<InspectionTicket>().selection_serial, 3u);
  }

  void sessionAccountsRetainedArtifacts() {
    FrameInspectionSession session;
    ManualExecutor executor;
    session.setExecutorForTesting(executor.dispatcher());

    auto target = built_target();
    QVERIFY(target != nullptr);
    session.selectTarget(target, frame_ticket(0));
    QCOMPARE(session.retainedBytes(), 0u);

    QSignalSpy ready(&session, &FrameInspectionSession::frameAnalysisReady);
    session.requestFrameAnalysis(frame_ticket(0));
    executor.run_all();
    QCOMPARE(ready.count(), 1);
    // The published analysis is accounted as retained (the shared_ptr
    // handed out stays pinned by the receiver until replaced).
    auto frame = ready.back().at(0).value<std::shared_ptr<const FrameStageSet>>();
    QVERIFY(frame != nullptr);
    const auto expected = frame->stages.filtered.size() +
                          frame->stages.unfiltered.size() +
                          frame->delivered.pixels.size();
    QCOMPARE(session.retainedBytes(), expected);

    session.clear(2);
    QCOMPARE(session.retainedBytes(), 0u);
  }
};

QTEST_MAIN(FrameInspectionSessionTest)
#include "frame_inspection_session_test.moc"
