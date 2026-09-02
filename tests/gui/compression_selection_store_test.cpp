// WP-5U12B Task 2: Current/Manual state and history store. Current mapping
// and Manual Selection coexist; manual and current updates never clear each
// other; stale generations and duplicate request serials emit nothing;
// history records every accepted navigation once and discards only the
// forward branch after going back.

#include <pnga/ui/qt/compression_selection_store.h>

#include <pnga/trace-model/compression_navigation.h>

#include <QtTest/QtTest>

#include <cstddef>
#include <cstdint>
#include <optional>

using pnga::trace_model::CompressionCurrentMapping;
using pnga::trace_model::CompressionNavigationOrigin;
using pnga::trace_model::CompressionNavigationTarget;
using pnga::trace_model::DocumentSourceUnit;
using pnga::trace_model::DocumentSourceUnitKind;
using pnga::trace_model::InflatedByteOffset;
using pnga::trace_model::InflatedByteRange;
using pnga::ui::qt::CompressionSelectionStore;

namespace {

CompressionNavigationTarget manual_target(std::uint64_t serial,
                                          std::uint64_t generation = 7) {
  CompressionNavigationTarget target;
  target.generation = generation;
  target.request_serial = serial;
  target.source_unit = DocumentSourceUnit{};
  target.origin = CompressionNavigationOrigin::kBlocks;
  target.logical_range =
      InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{2}};
  return target;
}

CompressionCurrentMapping current_mapping(std::uint64_t generation = 7) {
  CompressionCurrentMapping mapping;
  mapping.generation = generation;
  mapping.source_unit = DocumentSourceUnit{};
  mapping.output_range =
      InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{2}};
  return mapping;
}

}  // namespace

class CompressionSelectionStoreTest : public QObject {
  Q_OBJECT
 private slots:
  void currentAndManualCoexist();
  void staleAndInvalidRequestsEmitNothing();
  void duplicateSerialIsSuppressed();
  void historyNavigationDiscardsOnlyForwardBranch();
  void resetClearsStateHistoryAndEmitsOnce();
};

void CompressionSelectionStoreTest::currentAndManualCoexist() {
  CompressionSelectionStore store;
  QSignalSpy state_spy(&store, &CompressionSelectionStore::stateChanged);
  QSignalSpy nav_spy(&store, &CompressionSelectionStore::navigationRequested);

  store.resetGeneration(7);
  QCOMPARE(state_spy.count(), 1);
  QCOMPARE(nav_spy.count(), 0);
  QCOMPARE(store.state().generation, std::uint64_t{7});

  const CompressionCurrentMapping mapping = current_mapping();
  QVERIFY(store.setCurrent(mapping));
  QCOMPARE(state_spy.count(), 2);
  QVERIFY(store.state().current.has_value());
  QVERIFY(*store.state().current == mapping);
  QVERIFY(!store.state().manual.has_value());

  CompressionNavigationTarget manual = manual_target(1);
  manual.block_index = 1;
  QVERIFY(store.setManual(manual));
  QCOMPARE(state_spy.count(), 3);
  QCOMPARE(nav_spy.count(), 0);  // setManual is not a navigation request
  QVERIFY(store.state().current.has_value());
  QVERIFY(*store.state().current == mapping);
  QVERIFY(store.state().manual.has_value());
  QVERIFY(*store.state().manual == manual);

  // A later Current update preserves the user's Manual Selection.
  CompressionCurrentMapping mapping2 = mapping;
  mapping2.block_index = 2;
  QVERIFY(store.setCurrent(mapping2));
  QCOMPARE(state_spy.count(), 4);
  QVERIFY(*store.state().current == mapping2);
  QVERIFY(store.state().manual.has_value());
  QVERIFY(*store.state().manual == manual);

  // A later Manual selection preserves the Current mapping.
  CompressionNavigationTarget manual2 = manual_target(2);
  QVERIFY(store.setManual(manual2));
  QCOMPARE(state_spy.count(), 5);
  QVERIFY(*store.state().manual == manual2);
  QVERIFY(*store.state().current == mapping2);
  QCOMPARE(nav_spy.count(), 0);
}

void CompressionSelectionStoreTest::staleAndInvalidRequestsEmitNothing() {
  CompressionSelectionStore store;
  store.resetGeneration(8);
  QSignalSpy state_spy(&store, &CompressionSelectionStore::stateChanged);
  QSignalSpy nav_spy(&store, &CompressionSelectionStore::navigationRequested);

  QVERIFY(!store.setManual(manual_target(1, 7)));
  QVERIFY(!store.applyNavigation(manual_target(1, 7)));
  QVERIFY(!store.setCurrent(current_mapping(7)));
  QVERIFY(!store.applyNavigation(manual_target(0, 8)));
  QVERIFY(!store.setManual(manual_target(0, 8)));

  CompressionCurrentMapping empty_mapping = current_mapping(8);
  empty_mapping.output_range = InflatedByteRange{};
  QVERIFY(!store.setCurrent(empty_mapping));
  CompressionCurrentMapping bad_unit = current_mapping(8);
  bad_unit.source_unit = DocumentSourceUnit{DocumentSourceUnitKind::kFile, 3};
  QVERIFY(!store.setCurrent(bad_unit));

  QCOMPARE(state_spy.count(), 0);
  QCOMPARE(nav_spy.count(), 0);
  QCOMPARE(store.state().generation, std::uint64_t{8});
  QVERIFY(!store.state().current.has_value());
  QVERIFY(!store.state().manual.has_value());
  QVERIFY(store.history().empty());
}

void CompressionSelectionStoreTest::duplicateSerialIsSuppressed() {
  CompressionSelectionStore store;
  store.resetGeneration(7);
  QSignalSpy state_spy(&store, &CompressionSelectionStore::stateChanged);
  QSignalSpy nav_spy(&store, &CompressionSelectionStore::navigationRequested);

  CompressionNavigationTarget first = manual_target(5);
  QVERIFY(store.applyNavigation(first));
  QCOMPARE(nav_spy.count(), 1);
  QCOMPARE(state_spy.count(), 1);
  QCOMPARE(store.history().size(), 1);

  // The same serial was already applied: the echo is suppressed silently.
  QVERIFY(!store.applyNavigation(first));
  QVERIFY(!store.setManual(first));
  QCOMPARE(nav_spy.count(), 1);
  QCOMPARE(state_spy.count(), 1);
  QCOMPARE(store.history().size(), 1);

  CompressionNavigationTarget second = manual_target(6);
  QVERIFY(store.applyNavigation(second));
  QCOMPARE(nav_spy.count(), 2);
  QCOMPARE(state_spy.count(), 2);
}

void CompressionSelectionStoreTest::historyNavigationDiscardsOnlyForwardBranch() {
  CompressionSelectionStore store;
  store.resetGeneration(7);
  QVERIFY(!store.goBack());
  QVERIFY(!store.goForward());

  QSignalSpy state_spy(&store, &CompressionSelectionStore::stateChanged);
  QSignalSpy nav_spy(&store, &CompressionSelectionStore::navigationRequested);

  CompressionNavigationTarget a = manual_target(1);
  a.block_index = 1;
  CompressionNavigationTarget b = manual_target(2);
  b.block_index = 2;
  CompressionNavigationTarget c = manual_target(3);
  c.block_index = 3;

  QVERIFY(store.applyNavigation(a));
  QVERIFY(store.applyNavigation(b));
  QCOMPARE(store.history().size(), 2);
  QCOMPARE(store.historyIndex().value_or(std::size_t{99}), std::size_t{1});
  QVERIFY(*store.state().manual == b);
  QCOMPARE(state_spy.count(), 2);
  QCOMPARE(nav_spy.count(), 2);

  // Back re-emits the previous target once without duplicating history.
  QVERIFY(store.goBack());
  QCOMPARE(state_spy.count(), 3);
  QCOMPARE(nav_spy.count(), 3);
  QCOMPARE(store.history().size(), 2);
  QVERIFY(store.historyIndex().has_value());
  QCOMPARE(store.historyIndex().value(), std::size_t{0});
  QVERIFY(*store.state().manual == a);
  QVERIFY(nav_spy.at(2).at(0).value<CompressionNavigationTarget>() == a);

  QVERIFY(!store.goBack());
  QCOMPARE(state_spy.count(), 3);
  QCOMPARE(nav_spy.count(), 3);

  QVERIFY(store.goForward());
  QCOMPARE(state_spy.count(), 4);
  QCOMPARE(nav_spy.count(), 4);
  QCOMPARE(store.history().size(), 2);
  QCOMPARE(store.historyIndex().value(), std::size_t{1});
  QVERIFY(*store.state().manual == b);
  QVERIFY(nav_spy.at(3).at(0).value<CompressionNavigationTarget>() == b);

  QVERIFY(!store.goForward());
  QCOMPARE(state_spy.count(), 4);
  QCOMPARE(nav_spy.count(), 4);

  // A→B→Back→C produces A,C: only the forward branch is discarded.
  QVERIFY(store.goBack());
  QCOMPARE(state_spy.count(), 5);
  QCOMPARE(nav_spy.count(), 5);
  QVERIFY(*store.state().manual == a);
  QVERIFY(store.applyNavigation(c));
  QCOMPARE(state_spy.count(), 6);
  QCOMPARE(nav_spy.count(), 6);
  QCOMPARE(store.history().size(), 2);
  QCOMPARE(store.historyIndex().value(), std::size_t{1});
  QVERIFY(store.history()[0] == a);
  QVERIFY(store.history()[1] == c);
  QVERIFY(*store.state().manual == c);
}

void CompressionSelectionStoreTest::resetClearsStateHistoryAndEmitsOnce() {
  CompressionSelectionStore store;
  store.resetGeneration(7);
  QVERIFY(store.setCurrent(current_mapping()));
  QVERIFY(store.applyNavigation(manual_target(1)));
  QVERIFY(store.applyNavigation(manual_target(2)));
  QCOMPARE(store.history().size(), 2);

  QSignalSpy state_spy(&store, &CompressionSelectionStore::stateChanged);
  QSignalSpy nav_spy(&store, &CompressionSelectionStore::navigationRequested);

  store.resetGeneration(9);
  QCOMPARE(state_spy.count(), 1);
  QCOMPARE(nav_spy.count(), 0);
  QCOMPARE(store.state().generation, std::uint64_t{9});
  QVERIFY(!store.state().current.has_value());
  QVERIFY(!store.state().manual.has_value());
  QVERIFY(store.history().empty());
  QVERIFY(!store.historyIndex().has_value());
  QVERIFY(!store.goBack());
  QVERIFY(!store.goForward());

  // Old-generation requests cannot re-enter after the reset.
  QVERIFY(!store.applyNavigation(manual_target(1, 7)));
  QVERIFY(!store.setCurrent(current_mapping(7)));
  QCOMPARE(state_spy.count(), 1);
  QCOMPARE(nav_spy.count(), 0);
}

QTEST_MAIN(CompressionSelectionStoreTest)
#include "compression_selection_store_test.moc"
