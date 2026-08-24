// WP-104 Qt model test: row count, display roles and chunk spans of the
// ChunkModel over a physical Chunk index.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/ui/qt/chunk_model.h>

#include <QtTest/QtTest>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> chunk_bytes(const char* type, std::uint32_t length) {
  std::vector<std::byte> out;
  out.push_back(B(static_cast<unsigned char>(length >> 24)));
  out.push_back(B(static_cast<unsigned char>(length >> 16)));
  out.push_back(B(static_cast<unsigned char>(length >> 8)));
  out.push_back(B(static_cast<unsigned char>(length)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  for (std::uint32_t i = 0; i < length; ++i) {
    out.push_back(B(0x11));
  }
  out.push_back(B(0));
  out.push_back(B(0));
  out.push_back(B(0));
  out.push_back(B(0));
  return out;
}

pnga::png_format::ChunkIndex build_index() {
  std::vector<std::byte> data;
  data.assign(pnga::png_format::kPngSignature.begin(),
              pnga::png_format::kPngSignature.end());
  for (auto& c : {chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                  chunk_bytes("IEND", 0)}) {
    data.insert(data.end(), c.begin(), c.end());
  }
  static pnga::io::MemoryByteSource source(std::move(data));
  return pnga::png_format::index_chunks(source);
}

}  // namespace

class ChunkModelTest : public QObject {
  Q_OBJECT
 private slots:
  void rowCountMatchesIndex();
  void displayRolesExposeEnvelope();
  void chunkSpansAreExact();
};

void ChunkModelTest::rowCountMatchesIndex() {
  const pnga::png_format::ChunkIndex index = build_index();
  pnga::ui::qt::ChunkModel model(&index);
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.columnCount(), 5);
  QVERIFY(!model.index(0, 0).parent().isValid());
  QVERIFY(!model.index(3, 0).isValid());
}

void ChunkModelTest::displayRolesExposeEnvelope() {
  const pnga::png_format::ChunkIndex index = build_index();
  pnga::ui::qt::ChunkModel model(&index);

  QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toInt(), 1);
  QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toInt(), 2);
  QCOMPARE(model.data(model.index(2, 0), Qt::DisplayRole).toInt(), 3);
  QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(),
           QStringLiteral("IHDR"));
  QCOMPARE(model.data(model.index(1, 1), Qt::DisplayRole).toString(),
           QStringLiteral("IDAT"));
  QCOMPARE(model.data(model.index(2, 1), Qt::DisplayRole).toString(),
           QStringLiteral("IEND"));
  QCOMPARE(model.data(model.index(0, 2), Qt::DisplayRole).toULongLong(),
           qulonglong{13});
  QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
           QStringLiteral("#"));
  QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(),
           QStringLiteral("Type"));
  QVERIFY(model.data(model.index(0, 1), Qt::ToolTipRole).isNull());
}

void ChunkModelTest::chunkSpansAreExact() {
  const pnga::png_format::ChunkIndex index = build_index();
  pnga::ui::qt::ChunkModel model(&index);

  // IHDR envelope spans, byte-exact (matching the WP-101 golden layout).
  const auto& ihdr = model.chunkAt(0);
  QCOMPARE(ihdr.header_offset, qulonglong{8});
  QCOMPARE(ihdr.data_offset, qulonglong{16});
  QCOMPARE(ihdr.data_length, qulonglong{13});
  QCOMPARE(ihdr.crc_offset, qulonglong{29});

  const auto& iend = model.chunkAt(2);
  QCOMPARE(iend.data_length, qulonglong{0});
  QCOMPARE(iend.data_offset, iend.crc_offset);
}

QTEST_MAIN(ChunkModelTest)
#include "chunk_model_test.moc"
