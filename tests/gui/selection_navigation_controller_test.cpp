// WP-5U15 Task 6: selection/navigation contract — pixel commit publishes the
// lock and requests a trace once, hover never replays, hex source edits the
// shared view state.

#include "selection_navigation_controller.h"

#include <pnga/ui/qt/hex_source_tab_bar.h>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QMainWindow>
#include <QSpinBox>

#include <optional>

class SelectionNavigationControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void pixelCommitPublishesLockAndRequestsTrace();
  void hoverDoesNotRequestTrace();
  void hexSourceSelectionUpdatesViewState();
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

QTEST_MAIN(SelectionNavigationControllerTest)
#include "selection_navigation_controller_test.moc"
