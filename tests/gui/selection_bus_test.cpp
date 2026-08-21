// WP-205 SelectionBus tests: origin suppression, generation staleness and
// stress (100 alternating publications without recursion or stale state).

#include <pnga/ui/qt/selection_bus.h>

#include <pnga/trace-model/selection.h>

#include <QtTest/QtTest>

#include <cstdint>

using pnga::trace_model::ImageCoordinate;
using pnga::trace_model::Selection;
using pnga::ui::qt::SelectionBus;

namespace {

constexpr int kPanelA = 1;
constexpr int kPanelB = 2;

Selection chunk_sel(std::uint64_t row) {
  Selection s;
  s.node = row;
  s.stage = pnga::trace_model::Stage::kChunk;
  return s;
}

Selection pixel_sel(std::uint64_t x, std::uint64_t y) {
  Selection s;
  s.image = ImageCoordinate{0, 0, 0, x, y, 0};
  s.stage = pnga::trace_model::Stage::kDelivered;
  return s;
}

}  // namespace

class FakePanel : public QObject {
  Q_OBJECT
 public:
  explicit FakePanel(int myOrigin, QObject* parent = nullptr)
      : QObject(parent), myOrigin_(myOrigin) {}

 public slots:
  void onSelectionChanged(int origin, const Selection& selection) {
    if (origin == myOrigin_) {
      ownEchoes_++;  // panels must ignore their own publications
      return;
    }
    ++received_;
    lastSelection_ = selection;
  }

 public:
  int myOrigin_ = 0;
  int received_ = 0;
  int ownEchoes_ = 0;
  Selection lastSelection_;
};

class SelectionBusTest : public QObject {
  Q_OBJECT
 private slots:
  void panelsIgnoreTheirOwnPublications();
  void staleGenerationIsDropped();
  void hundredAlternatingPublicationsStayConsistent();
};

void SelectionBusTest::panelsIgnoreTheirOwnPublications() {
  SelectionBus bus;
  FakePanel a(kPanelA);
  FakePanel b(kPanelB);
  QObject::connect(&bus, &SelectionBus::selectionChanged, &a,
                   &FakePanel::onSelectionChanged);
  QObject::connect(&bus, &SelectionBus::selectionChanged, &b,
                   &FakePanel::onSelectionChanged);

  bus.setDocumentGeneration(1);
  bus.publish(kPanelA, 1, chunk_sel(3));

  QCOMPARE(a.received_, 0);  // A ignores its own echo
  QCOMPARE(a.ownEchoes_, 1);
  QCOMPARE(b.received_, 1);  // B receives it
  QCOMPARE(b.lastSelection_.node.value_or(0), std::uint64_t{3});
  QCOMPARE(bus.current(), chunk_sel(3));
}

void SelectionBusTest::staleGenerationIsDropped() {
  SelectionBus bus;
  FakePanel b(kPanelB);
  QObject::connect(&bus, &SelectionBus::selectionChanged, &b,
                   &FakePanel::onSelectionChanged);

  bus.setDocumentGeneration(1);
  bus.publish(kPanelA, 1, chunk_sel(1));
  QCOMPARE(b.received_, 1);

  // New document invalidates the old generation.
  bus.setDocumentGeneration(2);
  QVERIFY(bus.current().empty());

  // A publication tagged with the stale generation is dropped.
  bus.publish(kPanelA, 1, chunk_sel(9));
  QCOMPARE(b.received_, 1);
  QVERIFY(bus.current().empty());

  // Current generation passes; panel B receives A's publication (B never
  // publishes here, so it has no self-echoes).
  bus.publish(kPanelA, 2, pixel_sel(4, 5));
  QCOMPARE(b.received_, 2);
  QCOMPARE(b.ownEchoes_, 0);
  QCOMPARE(bus.current(), pixel_sel(4, 5));
}

void SelectionBusTest::hundredAlternatingPublicationsStayConsistent() {
  SelectionBus bus;
  FakePanel a(kPanelA);
  FakePanel b(kPanelB);
  QObject::connect(&bus, &SelectionBus::selectionChanged, &a,
                   &FakePanel::onSelectionChanged);
  QObject::connect(&bus, &SelectionBus::selectionChanged, &b,
                   &FakePanel::onSelectionChanged);

  bus.setDocumentGeneration(7);
  for (int i = 0; i < 100; ++i) {
    if (i % 2 == 0) {
      bus.publish(kPanelA, 7, chunk_sel(static_cast<std::uint64_t>(i)));
    } else {
      bus.publish(kPanelB, 7, pixel_sel(static_cast<std::uint64_t>(i),
                                        static_cast<std::uint64_t>(i)));
    }
  }

  // No recursion: each panel heard only the other's 50 publications plus its
  // own 50 echoes (which they ignored) — the bus emitted exactly 100 signals.
  QCOMPARE(a.received_, 50);
  QCOMPARE(b.received_, 50);
  QCOMPARE(a.ownEchoes_, 50);
  QCOMPARE(b.ownEchoes_, 50);
  // The last publication was from panel B.
  QCOMPARE(bus.current(), pixel_sel(99, 99));
}

QTEST_MAIN(SelectionBusTest)
#include "selection_bus_test.moc"
