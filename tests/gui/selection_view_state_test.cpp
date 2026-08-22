// WP-5U1 Qt-side view state tests: hover/locked ownership, preferences and
// stale-document coordinate clearing.

#include <pnga/ui/qt/selection_view_state.h>

#include <QtTest/QtTest>

using pnga::trace_model::ImageCoordinate;
using pnga::ui::qt::HexSource;
using pnga::ui::qt::NumericBase;
using pnga::ui::qt::SelectionViewState;

class SelectionViewStateTest : public QObject {
  Q_OBJECT
 private slots:
  void hoverAndLockAreIndependent();
  void preferencesStayInViewState();
  void generationChangeClearsCoordinates();
  void invalidPresentationCoordinateIsRejected();
};

void SelectionViewStateTest::hoverAndLockAreIndependent() {
  SelectionViewState state;
  QVERIFY(state.set_hover(ImageCoordinate{0, 0, 2, 3, 4}));
  QVERIFY(state.set_locked(ImageCoordinate{0, 0, 2, 3, 4}));
  QVERIFY(state.hover.has_value());
  QVERIFY(state.locked.has_value());

  state.clear_hover();
  QVERIFY(!state.hover.has_value());
  QVERIFY(state.locked.has_value());
}

void SelectionViewStateTest::preferencesStayInViewState() {
  SelectionViewState state;
  state.hex_source = HexSource::kIdatStream;
  state.numeric_base = NumericBase::kHexadecimal;
  state.hex_follow_pixel = false;
  QCOMPARE(state.hex_source, HexSource::kIdatStream);
  QCOMPARE(state.numeric_base, NumericBase::kHexadecimal);
  QVERIFY(!state.hex_follow_pixel);
}

void SelectionViewStateTest::generationChangeClearsCoordinates() {
  SelectionViewState state;
  QVERIFY(state.set_hover(ImageCoordinate{0, 0, 0, 1, 1}));
  QVERIFY(state.set_locked(ImageCoordinate{0, 0, 0, 1, 1}));
  state.set_document_generation(4);
  QVERIFY(!state.hover.has_value());
  QVERIFY(!state.locked.has_value());
  QVERIFY(state.set_locked(ImageCoordinate{0, 0, 0, 2, 2}));
  state.set_document_generation(4);  // same generation preserves state
  QVERIFY(state.locked.has_value());
  state.set_document_generation(5);
  QVERIFY(!state.locked.has_value());
}

void SelectionViewStateTest::invalidPresentationCoordinateIsRejected() {
  SelectionViewState state;
  ImageCoordinate invalid{0, 8, 0, 0, 0};
  QVERIFY(!state.set_hover(invalid));
  QVERIFY(!state.set_locked(invalid));
  QVERIFY(!state.hover.has_value());
  QVERIFY(!state.locked.has_value());
}

QTEST_MAIN(SelectionViewStateTest)
#include "selection_view_state_test.moc"
