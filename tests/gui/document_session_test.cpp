// WP-5U15 Task 5: DocumentSession must gate worker publication by document
// generation: a closed or replaced document never publishes stale results.

#include "document_session.h"

#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryFile>

class DocumentSessionTest : public QObject {
  Q_OBJECT
 private slots:
  void closeInvalidatesPendingWorkerPublication();
  void replacePublishesOnlyCurrentGeneration();

 private:
  static bool writeFixture(QTemporaryFile& png);
};

bool DocumentSessionTest::writeFixture(QTemporaryFile& png) {
  if (!png.open()) {
    return false;
  }
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  if (png.write(bytes) != bytes.size()) {
    return false;
  }
  png.flush();
  return true;
}

void DocumentSessionTest::closeInvalidatesPendingWorkerPublication() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  DocumentSession session;
  QSignalSpy decoded(&session, &DocumentSession::decodePublished);
  QVERIFY(session.replace(png.fileName()));
  const std::uint64_t opened = session.generation();
  session.startPrimaryWorkers();
  session.close();
  QCOMPARE(session.generation(), opened + 1);
  QVERIFY(!session.hasDocument());
  QTest::qWait(200);
  QCOMPARE(decoded.count(), 0);
}

void DocumentSessionTest::replacePublishesOnlyCurrentGeneration() {
  QTemporaryFile first;
  QVERIFY(writeFixture(first));
  QTemporaryFile second;
  QVERIFY(writeFixture(second));

  DocumentSession session;
  QSignalSpy decoded(&session, &DocumentSession::decodePublished);
  QVERIFY(session.replace(first.fileName()));
  session.startPrimaryWorkers();
  QVERIFY(session.replace(second.fileName()));
  const std::uint64_t generation = session.generation();
  session.startPrimaryWorkers();
  QTRY_VERIFY_WITH_TIMEOUT(decoded.count() >= 1, 5000);
  QCOMPARE(decoded.front().front().value<std::uint64_t>(), generation);
  QCOMPARE(decoded.front().front().value<std::uint64_t>(),
           session.generation());
  QTest::qWait(200);
}

QTEST_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
