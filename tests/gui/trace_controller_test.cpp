// WP-5U15 Task 7: bounded trace contract — identical committed intervals are
// submitted once, and a replacement never publishes the previous generation.

#include "trace_controller.h"

#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QMainWindow>
#include <QTemporaryFile>

#include <filesystem>
#include <memory>

static std::shared_ptr<pnga::io::IByteSource> mappedTraceFixture(
    QTemporaryFile& png) {
  if (!png.open()) return {};
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  if (png.write(bytes) != bytes.size()) return {};
  png.flush();
  std::unique_ptr<pnga::io::IByteSource> opened;
  if (pnga::io::open_mapped_file(
          std::filesystem::path(png.fileName().toStdString()), opened))
    return {};
  return std::shared_ptr<pnga::io::IByteSource>(opened.release());
}

static std::unique_ptr<pnga::analysis_engine::QueryCoordinator> readyQuery(
    const std::shared_ptr<pnga::io::IByteSource>& source) {
  const auto stages = pnga::analysis_engine::analyze_source(*source);
  auto query = std::make_unique<pnga::analysis_engine::QueryCoordinator>(
      2, 1ull << 26);
  const std::shared_ptr<const pnga::io::IByteSource> shared(source);
  if (!query->open(shared, stages.header, 16384)) return {};
  return query;
}

class TraceControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void identicalCommittedIntervalIsSubmittedOnce();
  void replacementDropsOldGenerationResult();
};

void TraceControllerTest::identicalCommittedIntervalIsSubmittedOnce() {
  QTemporaryFile png;
  const auto source = mappedTraceFixture(png);
  QVERIFY(source);
  const auto query = readyQuery(source);
  QVERIFY(query);
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  TraceController controller(widgets);
  controller.setQueryCoordinator(query.get());
  controller.replaceDocument(5, source);
  controller.requestFor({0, 0, 0, 0, 0});
  QTRY_COMPARE_WITH_TIMEOUT(
      controller.acceptedRequestCountForTest(), std::size_t{1}, 5000);
  controller.requestFor({0, 0, 0, 0, 0});
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{1});
}

void TraceControllerTest::replacementDropsOldGenerationResult() {
  QTemporaryFile png;
  const auto source = mappedTraceFixture(png);
  QVERIFY(source);
  const auto query = readyQuery(source);
  QVERIFY(query);
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  TraceController controller(widgets);
  controller.setQueryCoordinator(query.get());
  controller.replaceDocument(5, source);
  controller.requestFor({0, 0, 0, 0, 0});
  controller.replaceDocument(6, source);
  controller.setQueryCoordinator(query.get());
  controller.requestFor({0, 0, 0, 0, 0});
  // The fast-index publication alone already stamps the block view's
  // generation, so wait on a view that only the generation-6 trace bundle
  // updates before asserting the other two pages.
  QTRY_COMPARE_WITH_TIMEOUT(widgets.huffman_inspector->view().generation,
                            std::uint64_t{6}, 5000);
  QCOMPARE(widgets.block_inspector->view().generation, std::uint64_t{6});
  QCOMPARE(widgets.decode_trace_inspector->view().generation, std::uint64_t{6});
}

QTEST_MAIN(TraceControllerTest)
#include "trace_controller_test.moc"
