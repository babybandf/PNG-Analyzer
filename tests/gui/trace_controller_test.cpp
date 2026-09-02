// WP-5U15 Task 7: bounded trace contract — identical committed intervals are
// submitted once, and a replacement never publishes the previous generation.
// WP-5U12C: the explicit Open Decode Trace action submits through the same
// bounded path exactly once per block interval, while page switching, row
// selection, splitter/resize and DEC↔HEX changes submit zero replays.

#include "trace_controller.h"

#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTableView>
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
  void openDecodeTraceSubmitsOncePerBlockInterval();
  void incidentalInteractionSubmitsZero();
};

static QPushButton* findBlockButton(
    const pnga::ui::qt::BlockInspector* block, const QString& text) {
  const auto buttons = block->findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == text) {
      return button;
    }
  }
  return nullptr;
}

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
  QCOMPARE(widgets.decode_trace_inspector->view().scope.generation,
           std::uint64_t{6});
}

void TraceControllerTest::openDecodeTraceSubmitsOncePerBlockInterval() {
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

  auto* block = widgets.block_inspector;
  QVERIFY(block != nullptr);
  auto* table =
      block->findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  // The complete Fast Index rows are browsable without any trace request.
  QVERIFY(table->model()->rowCount() >= 1);
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{0});

  // The explicit action submits the bounded request exactly once for the
  // selected block and reuses the dedup for an identical interval.
  auto* trace_button =
      findBlockButton(block, QStringLiteral("Open Decode Trace"));
  QVERIFY(trace_button != nullptr);
  table->selectRow(0);
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{0});
  trace_button->click();
  QTRY_COMPARE_WITH_TIMEOUT(
      controller.acceptedRequestCountForTest(), std::size_t{1}, 5000);
  trace_button->click();
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{1});
}

void TraceControllerTest::incidentalInteractionSubmitsZero() {
  QTemporaryFile png;
  const auto source = mappedTraceFixture(png);
  QVERIFY(source);
  const auto query = readyQuery(source);
  QVERIFY(query);
  QMainWindow window;
  window.resize(1100, 700);
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  TraceController controller(widgets);
  controller.setQueryCoordinator(query.get());
  controller.replaceDocument(5, source);

  auto* block = widgets.block_inspector;
  QVERIFY(block != nullptr);
  auto* table =
      block->findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  table->selectRow(0);
  // Page switching, row selection, resize and DEC↔HEX submit zero replays.
  widgets.compression_inspector_tabs->setCurrentIndex(1);
  widgets.compression_inspector_tabs->setCurrentIndex(2);
  widgets.compression_inspector_tabs->setCurrentIndex(0);
  if (widgets.base_button != nullptr) {
    widgets.base_button->click();
    widgets.base_button->click();
  }
  window.resize(1000, 650);
  QTest::qWait(50);
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{0});
  QCOMPARE(controller.cancelledRequestCountForTest(), std::size_t{0});
  // An explicit Open Decode Trace is the only allowed submission.
  if (auto* trace_button =
          findBlockButton(block, QStringLiteral("Open Decode Trace"));
      trace_button != nullptr) {
    trace_button->click();
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.acceptedRequestCountForTest(), std::size_t{1}, 5000);
    // WP-5U12E: a Decode Trace row selection and every other incidental
    // action still submit zero replays.
    widgets.compression_inspector_tabs->setCurrentIndex(2);
    QTest::qWait(50);
    auto* trace_table = widgets.decode_trace_inspector->findChild<QTableView*>(
        QStringLiteral("compressionDecodeTraceTable"));
    QVERIFY(trace_table != nullptr);
    if (trace_table->model()->rowCount() > 0) {
      trace_table->selectRow(0);
      QTest::qWait(50);
    }
    widgets.compression_inspector_tabs->setCurrentIndex(0);
    window.resize(980, 640);
    QTest::qWait(50);
    QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{1});
    QCOMPARE(controller.cancelledRequestCountForTest(), std::size_t{0});
  }
}

QTEST_MAIN(TraceControllerTest)
#include "trace_controller_test.moc"
