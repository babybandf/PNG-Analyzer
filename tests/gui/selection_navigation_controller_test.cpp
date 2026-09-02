// WP-5U15 Task 6: selection/navigation contract — pixel commit publishes the
// lock and requests a trace once, hover never replays, hex source edits the
// shared view state.
// WP-5U12B Task 3: typed Compression navigation — generation/request-serial
// gates, loop suppression, variant routing and store-driven navigation submit
// zero trace work.

#include "selection_navigation_controller.h"

#include <pnga/io/byte_source.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QMainWindow>
#include <QSignalSpy>
#include <QSpinBox>

#include <cstring>
#include <optional>
#include <vector>

namespace {

// 1x1 grayscale PNG with a single IDAT (deterministic fixture).
constexpr const char* kPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUB"
    "AScY42YAAAAASUVORK5CYII=";

std::shared_ptr<pnga::io::MemoryByteSource> make_png_source() {
  const QByteArray bytes = QByteArray::fromBase64(kPngBase64);
  std::vector<std::byte> raw(static_cast<std::size_t>(bytes.size()));
  std::memcpy(raw.data(), bytes.constData(), raw.size());
  return std::make_shared<pnga::io::MemoryByteSource>(std::move(raw));
}

pnga::trace_model::CompressionNavigationTarget file_target(
    std::uint64_t generation, std::uint64_t serial) {
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = generation;
  target.request_serial = serial;
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kHex;
  target.logical_range =
      pnga::trace_model::FileByteRange{pnga::trace_model::FileByteOffset{0},
                                       pnga::trace_model::FileByteOffset{8}};
  target.physical_spans = {
      pnga::trace_model::FileByteRange{pnga::trace_model::FileByteOffset{0},
                                       pnga::trace_model::FileByteOffset{8}}};
  return target;
}

}  // namespace

class SelectionNavigationControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void pixelCommitPublishesLockAndRequestsTrace();
  void hoverDoesNotRequestTrace();
  void hexSourceSelectionUpdatesViewState();
  void typedNavigationRoutesFileAndGates();
  void typedNavigationInflatedUsesInflatedSource();
  void compressionCurrentFlowsThroughSharedStore();
};

void SelectionNavigationControllerTest::pixelCommitPublishesLockAndRequestsTrace() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  std::optional<pnga::trace_model::ImageCoordinate> coordinate;
  SelectionNavigationController controller(
      widgets,
      { [&trace_requests, &coordinate](const auto& value) {
          ++trace_requests;
          coordinate = value;
        }, [](std::uint64_t) {} });
  controller.setDocument(9, nullptr, nullptr, nullptr);
  controller.onPixelSelected(3, 4);
  QVERIFY(widgets.lock_check->isChecked());
  QCOMPARE(widgets.x_spin->value(), 3);
  QCOMPARE(widgets.y_spin->value(), 4);
  QCOMPARE(trace_requests, 1);
  QVERIFY(coordinate.has_value());
  QCOMPARE(coordinate->x, std::uint64_t{3});
  QCOMPARE(coordinate->y, std::uint64_t{4});
}

void SelectionNavigationControllerTest::hoverDoesNotRequestTrace() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  SelectionNavigationController controller(
      widgets,
      { [&trace_requests](const auto&) { ++trace_requests; },
        [](std::uint64_t) {} });
  controller.setDocument(9, nullptr, nullptr, nullptr);
  controller.onPixelHovered(7, 8);
  QCOMPARE(trace_requests, 0);
  QVERIFY(controller.viewState().hover.has_value());
  controller.onPixelHoverLeft();
  QCOMPARE(trace_requests, 0);
  QVERIFY(!controller.viewState().hover.has_value());
}

void SelectionNavigationControllerTest::hexSourceSelectionUpdatesViewState() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  SelectionNavigationController controller(
      widgets, { [](const auto&) {}, [](std::uint64_t) {} });
  controller.setHexSource(pnga::ui::qt::HexSource::kInflated);
  QCOMPARE(controller.viewState().hex_source,
           pnga::ui::qt::HexSource::kInflated);
  QCOMPARE(widgets.hex_source_tabs->source(),
           pnga::ui::qt::HexSource::kInflated);
}

void SelectionNavigationControllerTest::typedNavigationRoutesFileAndGates() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  SelectionNavigationController controller(
      widgets,
      {[&trace_requests](const auto&) { ++trace_requests; },
       [](std::uint64_t) {}});
  auto source = make_png_source();
  controller.setDocument(5, source, nullptr, nullptr);
  controller.refreshHexSource();

  QSignalSpy location_spy(widgets.hex, &pnga::ui::qt::HexView::locationChanged);

  controller.applyCompressionNavigation(file_target(5, 1));
  QCOMPARE(location_spy.count(), 1);  // one request gives one view update
  QCOMPARE(widgets.hex_source_tabs->source(),
           pnga::ui::qt::HexSource::kFile);
  QCOMPARE(widgets.hex->currentLocation().value_or(99), std::uint64_t{0});

  // A serial already applied by the receiver is suppressed (loop guard).
  controller.applyCompressionNavigation(file_target(5, 1));
  QCOMPARE(location_spy.count(), 1);

  // A stale generation is ignored before any UI publication.
  controller.applyCompressionNavigation(file_target(4, 2));
  QCOMPARE(location_spy.count(), 1);

  // After the document moved to generation 6, generation-5 targets are stale.
  controller.setDocument(6, source, nullptr, nullptr);
  controller.refreshHexSource();
  controller.applyCompressionNavigation(file_target(5, 3));
  QCOMPARE(location_spy.count(), 1);

  controller.applyCompressionNavigation(file_target(6, 4));
  QCOMPARE(location_spy.count(), 2);
  QCOMPARE(trace_requests, 0);  // navigation submits zero trace work
}

void SelectionNavigationControllerTest::typedNavigationInflatedUsesInflatedSource() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  SelectionNavigationController controller(
      widgets,
      {[&trace_requests](const auto&) { ++trace_requests; },
       [](std::uint64_t) {}});
  auto source = make_png_source();
  controller.setDocument(5, source, nullptr, nullptr);
  controller.refreshHexSource();
  controller.applyCompressionNavigation(file_target(5, 1));
  QCOMPARE(widgets.hex_source_tabs->source(), pnga::ui::qt::HexSource::kFile);

  pnga::trace_model::CompressionNavigationTarget inflated;
  inflated.generation = 5;
  inflated.request_serial = 2;
  inflated.origin =
      pnga::trace_model::CompressionNavigationOrigin::kInflated;
  inflated.logical_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{0},
      pnga::trace_model::InflatedByteOffset{2}};
  controller.applyCompressionNavigation(inflated);
  // An Inflated range routes through the existing Inflated source only.
  QCOMPARE(widgets.hex_source_tabs->source(),
           pnga::ui::qt::HexSource::kInflated);
  QCOMPARE(trace_requests, 0);
}

void SelectionNavigationControllerTest::compressionCurrentFlowsThroughSharedStore() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  pnga::ui::qt::CompressionSelectionStore store;
  SelectionNavigationController controller(
      widgets,
      {[&trace_requests](const auto&) { ++trace_requests; },
       [](std::uint64_t) {}},
      nullptr, nullptr, &store);

  auto source = make_png_source();
  controller.setDocument(7, source, nullptr, nullptr);
  controller.refreshHexSource();
  QCOMPARE(store.state().generation, std::uint64_t{7});

  // Manual selection and the Current mapping coexist in the shared store.
  auto manual = file_target(7, 1);
  manual.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  QVERIFY(store.setManual(manual));
  pnga::trace_model::CompressionCurrentMapping mapping;
  mapping.generation = 7;
  mapping.output_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{0},
      pnga::trace_model::InflatedByteOffset{1}};
  controller.setCompressionCurrent(mapping);
  QVERIFY(store.state().current.has_value());
  QVERIFY(*store.state().current == mapping);
  QVERIFY(store.state().manual.has_value());
  QVERIFY(*store.state().manual == manual);

  // A stale Current mapping is ignored.
  pnga::trace_model::CompressionCurrentMapping stale = mapping;
  stale.generation = 6;
  controller.setCompressionCurrent(stale);
  QVERIFY(*store.state().current == mapping);

  // Store-driven navigation reaches the hex view without trace work.
  QSignalSpy location_spy(widgets.hex, &pnga::ui::qt::HexView::locationChanged);
  QVERIFY(store.applyNavigation(file_target(7, 2)));
  QCOMPARE(location_spy.count(), 1);
  QCOMPARE(widgets.hex->currentLocation().value_or(99), std::uint64_t{0});
  QCOMPARE(widgets.hex_source_tabs->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(trace_requests, 0);
}

QTEST_MAIN(SelectionNavigationControllerTest)
#include "selection_navigation_controller_test.moc"
