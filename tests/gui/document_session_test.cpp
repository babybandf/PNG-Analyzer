// WP-5U15 Task 5: DocumentSession must gate worker publication by document
// generation: a closed or replaced document never publishes stale results.
// WP-602G Task 8: lazy statistics lifecycle — no statistics worker during
// open/startPrimaryWorkers, a request made before the stage analysis waits
// and starts once, replace/close cancel collection without stale
// publication, and repeated requests never duplicate the worker.

#include "document_session.h"
#include "statistics_worker.h"

#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryFile>

class DocumentSessionTest : public QObject {
  Q_OBJECT
 private slots:
  void closeInvalidatesPendingWorkerPublication();
  void replacePublishesOnlyCurrentGeneration();
  void statisticsNotStartedDuringOpen();
  void statisticsRequestWaitsForStagesThenStartsOnce();
  void replaceAndCloseDropStaleStatisticsPublication();

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

void DocumentSessionTest::statisticsNotStartedDuringOpen() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  DocumentSession session;
  QSignalSpy progress(&session, &DocumentSession::statisticsProgress);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(progress.isValid() && finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  QTest::qWait(300);
  // Opening a document and running the primary workers starts no statistics
  // work (lazy contract): no worker exists and nothing is published.
  QVERIFY(session.findChildren<StatisticsWorker*>().isEmpty());
  QCOMPARE(progress.count(), 0);
  QCOMPARE(finished.count(), 0);
}

void DocumentSessionTest::statisticsRequestWaitsForStagesThenStartsOnce() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  DocumentSession session;
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  // Request before the stage analysis: nothing starts while stages are
  // missing.
  session.requestStatistics();
  QVERIFY(session.findChildren<StatisticsWorker*>().isEmpty());
  QTest::qWait(100);
  QCOMPARE(finished.count(), 0);
  // Once the stages publish, the pending request runs to one final result.
  session.startPrimaryWorkers();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
  QCOMPARE(finished.front().front().value<std::uint64_t>(),
           session.generation());
  // requestStatistics() creates the worker synchronously, so an immediate
  // repeat observes exactly one worker — a running worker is never
  // duplicated. (deleteLater needs the event loop, so the fresh worker is
  // still a child here.)
  session.requestStatistics();
  QCOMPARE(session.findChildren<StatisticsWorker*>().size(), 1);
  session.requestStatistics();
  QCOMPARE(session.findChildren<StatisticsWorker*>().size(), 1);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, 10000);
}

void DocumentSessionTest::replaceAndCloseDropStaleStatisticsPublication() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));
  QTemporaryFile second;
  QVERIFY(writeFixture(second));

  DocumentSession session;
  QSignalSpy progress(&session, &DocumentSession::statisticsProgress);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(progress.isValid() && finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  QTRY_VERIFY_WITH_TIMEOUT(session.stageSet() != nullptr, 10000);
  session.requestStatistics();
  QCOMPARE(session.findChildren<StatisticsWorker*>().size(), 1);
  // Replacing during collection cancels it; the queued result of the old
  // generation is dropped before any subscriber sees it.
  QVERIFY(session.replace(second.fileName()));
  QTest::qWait(300);
  QCOMPARE(progress.count(), 0);
  QCOMPARE(finished.count(), 0);
  // The replacement does not start statistics by itself, so the second
  // document's pending request only runs once its stages are requested.
  QVERIFY(session.findChildren<StatisticsWorker*>().isEmpty());

  // Closing during a new collection cancels it too, with no publication.
  session.startPrimaryWorkers();
  QTRY_VERIFY_WITH_TIMEOUT(session.stageSet() != nullptr, 10000);
  session.requestStatistics();
  QCOMPARE(session.findChildren<StatisticsWorker*>().size(), 1);
  session.close();
  QTest::qWait(300);
  QCOMPARE(progress.count(), 0);
  QCOMPARE(finished.count(), 0);
}

QTEST_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
