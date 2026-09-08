#include "animation_controller.h"

#include <pnga/io/byte_source.h>
#include <pnga/png-format/animation_index.h>

#include <QtTest/QtTest>

#include <array>
#include <memory>

#include "apng_fixture.h"

class AnimationControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void publishesOnlyTheCurrentSerial();
  void classifiesStaticAndPartialDocuments();
};

namespace {

std::array<pnga::png_format::FrameControl, 2> controls() {
  return std::array<pnga::png_format::FrameControl, 2>{
      pnga::png_format::FrameControl{0, 1, 1, 0, 0, 1, 100, 0, 0},
      pnga::png_format::FrameControl{0, 1, 1, 0, 0, 2, 100, 0, 1},
  };
}

pnga::analysis_engine::FrameRequest request_for(
    std::vector<std::byte> bytes) {
  auto source = std::make_shared<const pnga::io::MemoryByteSource>(
      std::move(bytes));
  auto index = std::make_shared<const pnga::png_format::AnimationIndex>(
      pnga::png_format::index_animation(*source,
                                         pnga::png_format::AnimationLimits{},
                                         [] { return false; }));
  pnga::analysis_engine::FrameRequest request;
  request.source = std::move(source);
  request.index = std::move(index);
  request.canvas_header = {1, 1, 8, 6, false};
  return request;
}

}  // namespace

void AnimationControllerTest::publishesOnlyTheCurrentSerial() {
  AnimationController controller;
  QSignalSpy published(&controller, &AnimationController::framePublished);
  controller.setDocument(
      request_for(pnga_test::make_apng(false, controls())));
  controller.selectFrame(0);
  controller.selectFrame(1);

  auto old = std::make_shared<pnga::analysis_engine::ReplayResult>();
  old->generation = controller.generation();
  old->request_serial = controller.requestSerial() - 1;
  old->identity = pnga::trace_model::AnimationFrame{0};
  auto current = std::make_shared<pnga::analysis_engine::ReplayResult>();
  current->generation = controller.generation();
  current->request_serial = controller.requestSerial();
  current->identity = pnga::trace_model::AnimationFrame{1};
  controller.publishWorkerResultForTesting(old);
  controller.publishWorkerResultForTesting(current);
  QCOMPARE(published.count(), 1);
}

void AnimationControllerTest::classifiesStaticAndPartialDocuments() {
  AnimationController controller;
  QSignalSpy capabilities(&controller, &AnimationController::capabilityChanged);
  controller.setDocument(request_for(pnga_test::make_apng(false, {})));
  QCOMPARE(static_cast<int>(controller.capability()),
           static_cast<int>(AnimationController::Capability::kError));
  Q_UNUSED(capabilities);
}

QTEST_MAIN(AnimationControllerTest)
#include "animation_controller_test.moc"
