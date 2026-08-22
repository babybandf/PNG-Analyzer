// WP-5U4A source abstraction tests: File ownership and virtual IDAT windows.

#include <pnga/io/byte_source.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/ui/qt/hex_data_source.h>
#include <pnga/ui/qt/hex_view.h>

#include <QtTest/QtTest>

#include <array>
#include <memory>
#include <vector>

class HexDataSourceTest : public QObject {
  Q_OBJECT
 private slots:
  void fileSourceReadsAndKeepsBackingAlive();
  void idatSourceReadsAcrossSegmentsWithoutConcatenation();
  void derivedSourcesExposeStageBytesAndStates();
  void hexViewKeepsBoundedAddressHistory();
};

void HexDataSourceTest::fileSourceReadsAndKeepsBackingAlive() {
  auto backing = std::make_shared<pnga::io::MemoryByteSource>(
      std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});
  const auto source = pnga::ui::qt::make_file_hex_source(backing);
  backing.reset();
  QCOMPARE(source->name(), "File");
  QCOMPARE(source->size(), 3U);
  std::array<std::byte, 2> bytes{};
  QVERIFY(source->read(1, bytes.data(), bytes.size()));
  QCOMPARE(static_cast<unsigned>(bytes[0]), 2U);
  QCOMPARE(static_cast<unsigned>(bytes[1]), 3U);
}

void HexDataSourceTest::idatSourceReadsAcrossSegmentsWithoutConcatenation() {
  auto backing = std::make_shared<pnga::io::MemoryByteSource>(
      std::vector<std::byte>{std::byte{9}, std::byte{10}, std::byte{11},
                              std::byte{12}, std::byte{13}, std::byte{14}});
  pnga::png_format::ChunkIndex index;
  index.chunks.push_back(pnga::png_format::ChunkNode{
      0, 1, 2, 3, {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
                   std::byte{'T'}}});
  index.chunks.push_back(pnga::png_format::ChunkNode{
      0, 4, 2, 6, {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
                   std::byte{'T'}}});
  const pnga::png_format::VirtualIDATStream stream(index);
  const auto source = pnga::ui::qt::make_idat_hex_source(backing, stream);
  QCOMPARE(source->name(), "IDAT Stream");
  QCOMPARE(source->size(), 4U);
  std::array<std::byte, 4> bytes{};
  QVERIFY(source->read(0, bytes.data(), bytes.size()));
  QCOMPARE(static_cast<unsigned>(bytes[0]), 10U);
  QCOMPARE(static_cast<unsigned>(bytes[1]), 11U);
  QCOMPARE(static_cast<unsigned>(bytes[2]), 13U);
  QCOMPARE(static_cast<unsigned>(bytes[3]), 14U);
}

void HexDataSourceTest::derivedSourcesExposeStageBytesAndStates() {
  auto unavailable = pnga::ui::qt::make_inflated_hex_source(nullptr);
  QCOMPARE(unavailable->status(),
           pnga::ui::qt::HexDataStatus::kUnavailable);

  auto failed = std::make_shared<pnga::analysis_engine::StageSet>();
  failed->success = false;
  auto error = pnga::ui::qt::make_defiltered_hex_source(failed);
  QCOMPARE(error->status(), pnga::ui::qt::HexDataStatus::kError);

  auto ready = std::make_shared<pnga::analysis_engine::StageSet>();
  ready->success = true;
  ready->filtered = {std::byte{7}, std::byte{8}};
  ready->unfiltered = {std::byte{9}, std::byte{10}};
  const auto inflated = pnga::ui::qt::make_inflated_hex_source(ready);
  const auto defiltered =
      pnga::ui::qt::make_defiltered_hex_source(ready);
  QCOMPARE(inflated->status(), pnga::ui::qt::HexDataStatus::kReady);
  QCOMPARE(defiltered->status(), pnga::ui::qt::HexDataStatus::kReady);
  std::array<std::byte, 2> bytes{};
  QVERIFY(inflated->read(0, bytes.data(), bytes.size()));
  QCOMPARE(static_cast<unsigned>(bytes[0]), 7U);
  QVERIFY(defiltered->read(0, bytes.data(), bytes.size()));
  QCOMPARE(static_cast<unsigned>(bytes[0]), 9U);
}

void HexDataSourceTest::hexViewKeepsBoundedAddressHistory() {
  auto backing = std::make_shared<pnga::io::MemoryByteSource>(
      std::vector<std::byte>(64, std::byte{0}));
  pnga::ui::qt::HexView view;
  view.setSource(pnga::ui::qt::make_file_hex_source(backing));
  QVERIFY(!view.currentLocation().has_value());
  QVERIFY(view.navigateTo(3));
  QVERIFY(view.navigateTo(20));
  QCOMPARE(view.currentLocation(), std::optional<std::uint64_t>(20));
  QVERIFY(view.goBack());
  QCOMPARE(view.currentLocation(), std::optional<std::uint64_t>(3));
  QVERIFY(view.goForward());
  QCOMPARE(view.currentLocation(), std::optional<std::uint64_t>(20));
  QVERIFY(!view.navigateTo(64));
  QVERIFY(!view.goForward());
}

QTEST_MAIN(HexDataSourceTest)
#include "hex_data_source_test.moc"
