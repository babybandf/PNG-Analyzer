// WP-5U15 Task 2: the extracted worker types must preserve their request
// identity (generation / selection serial) verbatim from the facade header.

#include "document_workers.h"

#include <QtTest/QtTest>

class DocumentWorkersTest : public QObject {
  Q_OBJECT
 private slots:
  void workersPreserveRequestIdentity();
};

void DocumentWorkersTest::workersPreserveRequestIdentity() {
  DecodeWorker decode(17, nullptr);
  StageWorker stages(18, nullptr);
  pnga::png_format::ChunkIndex index;
  ValidationWorker validation(19, nullptr, index);
  pnga::png_format::ChunkNode node;
  ChunkDetailWorker detail(20, 21, nullptr, node);
  QCOMPARE(decode.generation(), std::uint64_t{17});
  QCOMPARE(stages.generation(), std::uint64_t{18});
  QCOMPARE(validation.generation(), std::uint64_t{19});
  QCOMPARE(detail.generation(), std::uint64_t{20});
  QCOMPARE(detail.selectionSerial(), std::uint64_t{21});
}

QTEST_MAIN(DocumentWorkersTest)
#include "document_workers_test.moc"
