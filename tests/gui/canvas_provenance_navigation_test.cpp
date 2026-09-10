// WP-APNG-INSPECT T10: evidence focus and cross-panel provenance
// navigation. The frame1 PostBlend value is contributed by frame 0 through
// the C4 DAG; selecting the contributing FrameSample attaches an evidence
// focus to the session with an independent monotonic serial, and stale
// focus results are rejected without touching the main flow.

#include "frame_inspection_session.h"

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/canvas_pixel_query.h>
#include <pnga/trace-model/inspection_context.h>
#include <pnga/trace-model/selection.h>

#include <QtTest/QtTest>

#include <QSignalSpy>

#include <memory>

#include "apng_inspection_fixture.h"

using pnga::analysis_engine::AnalysisTarget;
using pnga::analysis_engine::CanvasOperation;
using pnga::analysis_engine::CanvasPixelRequest;
using pnga::analysis_engine::CanvasPixelResult;
using pnga::gui::FrameInspectionSession;
using pnga::trace_model::AnalysisKey;
using pnga::trace_model::AnimationFrame;
using pnga::trace_model::InspectionTicket;
using pnga::trace_model::Selection;
using pnga::trace_model::Stage;

Q_DECLARE_METATYPE(AnalysisKey)
Q_DECLARE_METATYPE(Selection)
Q_DECLARE_METATYPE(InspectionTicket)

namespace {

constexpr const char* kFingerprint = "fnv1a64-v1:0123456789abcdef";

InspectionTicket frame_ticket(std::uint32_t index, std::uint64_t serial) {
  return InspectionTicket{{7, AnimationFrame{index}},
                          Stage::kPreBlend, 1, serial};
}

// Runs the C4 query for frame1's PostBlend pixel and returns the DAG.
CanvasPixelResult frame1_provenance() {
  CanvasPixelRequest request;
  request.document = pnga_test::inspection_request(1);
  request.ticket = frame_ticket(1, 4);
  request.x = 11;
  request.y = 22;
  return query_canvas_pixel(request, nullptr);
}

// The FrameSample node of the contributing frame 0 inside the DAG (the
// source of the PreBlend chain feeding frame 1's blend).
const pnga::analysis_engine::CanvasPixelNode* find_frame0_sample(
    const CanvasPixelResult& result) {
  for (const auto& node : result.nodes) {
    if (node.operation == CanvasOperation::kFrameSample && node.frame == 0) {
      return &node;
    }
  }
  return nullptr;
}

}  // namespace

class CanvasProvenanceNavigationTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<AnalysisKey>("AnalysisKey");
    qRegisterMetaType<Selection>("Selection");
    qRegisterMetaType<InspectionTicket>("InspectionTicket");
  }

  void frame1PostBlendIsContributedByFrame0() {
    // The C4 DAG for frame 1's PostBlend pixel reaches frame 0's own pixel
    // through the PreBlend -> PostDispose(0) -> PostBlend(0) chain: the
    // canvas value is genuinely contributed by the earlier frame.
    const auto result = frame1_provenance();
    QCOMPARE(result.stop, CanvasPixelResult::Stop::kReady);
    const auto* source = find_frame0_sample(result);
    QVERIFY(source != nullptr);
    QCOMPARE(source->stage, Stage::kFrameOutput);
  }

  void focusAttachesWithIndependentSerialAndRejectsStaleResults() {
    FrameInspectionSession session;
    auto request = pnga_test::inspection_request(1);
    auto target = pnga::analysis_engine::make_frame_target(request);
    QVERIFY(target.target);
    const InspectionTicket main_ticket = frame_ticket(1, 4);
    session.selectTarget(target.target, main_ticket);

    QSignalSpy changed(&session,
                       &FrameInspectionSession::evidenceFocusChanged);
    QSignalSpy cleared(&session, &FrameInspectionSession::evidenceFocusCleared);

    // Selecting the contributing FrameSample attaches a focus on frame 0.
    const auto result = frame1_provenance();
    const auto* source = find_frame0_sample(result);
    QVERIFY(source != nullptr);
    Selection source_selection;
    source_selection.image = pnga::trace_model::ImageCoordinate{};
    source_selection.image->identity = AnimationFrame{0};
    source_selection.image->x = 11;
    source_selection.image->y = 22;
    source_selection.stage = Stage::kFrameOutput;
    const AnalysisKey source_key{7, AnimationFrame{0}};

    session.setEvidenceFocus(source_key, source_selection, main_ticket);
    QCOMPARE(changed.count(), 1);
    QVERIFY(session.evidenceFocusActive());
    const auto focus_serial = session.evidenceFocusSerial();
    QVERIFY(focus_serial > 0);
    QVERIFY(session.evidenceAccepts(focus_serial));
    // The main ticket is unchanged and still accepted.
    QVERIFY(session.accepts(main_ticket, pnga::trace_model::PublicationScope::kTarget));

    // A stale focus result (older serial) never publishes.
    QVERIFY(!session.evidenceAccepts(focus_serial - 1));

    // Attaching a new focus bumps the independent serial monotonically and
    // rejects the previous one.
    session.setEvidenceFocus(source_key, source_selection, main_ticket);
    QVERIFY(session.evidenceFocusSerial() > focus_serial);
    QVERIFY(!session.evidenceAccepts(focus_serial));

    // Clearing the focus rejects everything focus-scoped.
    session.clearEvidenceFocus();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(!session.evidenceFocusActive());
    QVERIFY(!session.evidenceAccepts(session.evidenceFocusSerial()));
  }

  void switchingMainContextClearsTheFocus() {
    FrameInspectionSession session;
    auto request = pnga_test::inspection_request(1);
    auto target = pnga::analysis_engine::make_frame_target(request);
    QVERIFY(target.target);
    session.selectTarget(target.target, frame_ticket(1, 4));

    Selection selection;
    selection.image = pnga::trace_model::ImageCoordinate{};
    selection.image->identity = AnimationFrame{0};
    session.setEvidenceFocus(AnalysisKey{7, AnimationFrame{0}}, selection,
                             frame_ticket(1, 4));
    QVERIFY(session.evidenceFocusActive());

    // Switching the main pixel/context clears the focus (C10).
    auto request0 = pnga_test::inspection_request(0);
    auto target0 = pnga::analysis_engine::make_frame_target(request0);
    QVERIFY(target0.target);
    session.selectTarget(target0.target, frame_ticket(0, 5));
    QVERIFY(!session.evidenceFocusActive());

    // A focus cannot attach to a foreign parent ticket.
    session.setEvidenceFocus(AnalysisKey{7, AnimationFrame{0}}, selection,
                             frame_ticket(1, 4));
    QVERIFY(!session.evidenceFocusActive());
  }
};

QTEST_MAIN(CanvasProvenanceNavigationTest)
#include "canvas_provenance_navigation_test.moc"
