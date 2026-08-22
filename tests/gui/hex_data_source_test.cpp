// WP-5U4A source abstraction tests: File ownership and virtual IDAT windows.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/ui/qt/hex_data_source.h>

#include <QtTest/QtTest>

#include <array>
#include <memory>
#include <vector>

class HexDataSourceTest : public QObject {
  Q_OBJECT
 private slots:
  void fileSourceReadsAndKeepsBackingAlive();
  void idatSourceReadsAcrossSegmentsWithoutConcatenation();
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

QTEST_MAIN(HexDataSourceTest)
#include "hex_data_source_test.moc"
