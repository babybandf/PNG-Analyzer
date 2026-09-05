// WP-602G Task 8: StatisticsController tests. The controller must start
// collection only on the first Statistics-tab activation, Refresh or Export
// (never on file open), generation-gate every publication, keep verified
// rows on Cancel, export byte-identical JSON/CSV through the shared
// serializer with QSaveFile atomic replacement (a write failure never
// overwrites the target), label partial output, and publish accepted
// occurrence results exactly once through the existing SelectionBus while
// stale results publish nothing.

#include "document_session.h"
#include "main_window_ui.h"
#include "statistics_controller.h"

#include <pnga/analysis-engine/statistics_collector.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/statistics/serialization.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/statistics_inspector.h>
#include <pnga/ui/qt/statistics_table_model.h>

#include <QtTest/QtTest>

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace {

using pnga::analysis_engine::StatisticsCollectionResult;

constexpr const char* kOneByOnePngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBASc"
    "Y42YAAAAASUVORK5CYII=";

bool writeFixture(QTemporaryFile& png, bool truncate = false) {
  if (!png.open()) {
    return false;
  }
  const QByteArray bytes = QByteArray::fromBase64(kOneByOnePngBase64);
  const QByteArray payload =
      truncate ? bytes.left(static_cast<int>(bytes.size()) * 2 / 3) : bytes;
  if (png.write(payload) != payload.size()) {
    return false;
  }
  png.flush();
  return true;
}

// Records SelectionBus publications without requiring a Selection metatype.
class BusRecorder final : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

 public slots:
  void onSelectionChanged(int origin,
                          const pnga::trace_model::Selection& selection) {
    ++count;
    last_origin = origin;
    last = selection;
  }

 public:
  int count = 0;
  int last_origin = 0;
  pnga::trace_model::Selection last;
};

QPushButton* inspectorButton(MainWindowWidgets& widgets, const char* name) {
  return widgets.statistics_inspector->findChild<QPushButton*>(
      QString::fromLatin1(name));
}

QLabel* progressLabel(MainWindowWidgets& widgets) {
  return widgets.statistics_inspector->findChild<QLabel*>(
      QStringLiteral("statisticsProgress"));
}

pnga::ui::qt::StatisticsTableModel* chunksModel(MainWindowWidgets& widgets) {
  return static_cast<pnga::ui::qt::StatisticsTableModel*>(
      widgets.statistics_inspector
          ->findChild<QTableView*>(QStringLiteral("statisticsChunksTable"))
          ->model());
}

int rowById(pnga::ui::qt::StatisticsTableModel* model, const char* id) {
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->rowAt(row)->id == id) {
      return row;
    }
  }
  return -1;
}

// Synchronous reference collection through the shared pipeline for byte
// equality checks.
StatisticsCollectionResult collectReference(const QString& path) {
  std::unique_ptr<pnga::io::IByteSource> opened;
  const std::error_code ec = pnga::io::open_mapped_file(
      std::filesystem::path(path.toStdString()), opened);
  Q_ASSERT(ec.value() == 0);
  std::shared_ptr<const pnga::io::IByteSource> source(std::move(opened));
  pnga::analysis_engine::StatisticsCollectionRequest request;
  request.source = source;
  request.chunks = pnga::png_format::index_chunks(*source);
  request.stages = std::make_shared<const pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source));
  return pnga::analysis_engine::collect_document_statistics(request, nullptr,
                                                            {});
}

}  // namespace

class StatisticsControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void noCollectionBeforeSelectingStatistics();
  void refreshStartsNewGenerationScopedRequest();
  void cancelRetainsVerifiedRows();
  void exportMatchesSharedSerializer();
  void partialExportRemainsEnabledAndLabeled();
  void exportFailureLeavesTargetUntouched();
  void occurrencePublishesOnceAndStalePublishesNothing();
};

void StatisticsControllerTest::noCollectionBeforeSelectingStatistics() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  QTest::qWait(300);
  // Opening the file and publishing the stages starts no statistics work.
  QCOMPARE(finished.count(), 0);

  // One lazy request on first selection; the result is generation-gated.
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
  QCOMPARE(finished.front().front().value<std::uint64_t>(),
           session.generation());

  // Re-activation after the result published must not re-request.
  widgets.inspector_tabs->setCurrentIndex(0);
  QTest::qWait(100);
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTest::qWait(200);
  QCOMPARE(finished.count(), 1);
}

void StatisticsControllerTest::refreshStartsNewGenerationScopedRequest() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);

  auto* refresh = inspectorButton(widgets, "statisticsRefresh");
  QVERIFY(refresh != nullptr);
  refresh->click();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, 10000);
  QCOMPARE(finished.back().front().value<std::uint64_t>(),
           session.generation());
}

void StatisticsControllerTest::cancelRetainsVerifiedRows() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
  // The ready result carries a verified chunks table.
  auto* model = chunksModel(widgets);
  int count_row = rowById(model, "chunks.count");
  QVERIFY(count_row >= 0);
  // The fixture carries IHDR, IDAT and IEND.
  QCOMPARE(model->rowAt(count_row)->value, std::optional<std::uint64_t>{3});

  // Refresh starts a new run; Cancel stops it. The final publication of the
  // same generation arrives either way, the table is never cleared, and the
  // chunks row keeps its frozen outcome: ready 3 when the run beat the
  // cancel, or the cancelled zero verified prefix otherwise.
  inspectorButton(widgets, "statisticsRefresh")->click();
  inspectorButton(widgets, "statisticsCancel")->click();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, 10000);
  QVERIFY(model->rowCount() >= 2);
  count_row = rowById(model, "chunks.count");
  QVERIFY(count_row >= 0);
  {
    const QString status = model->data(model->index(count_row, pnga::ui::qt::StatisticsTableModel::Status),
                                       Qt::DisplayRole).toString();
    QVERIFY(status == QStringLiteral("ready") || status == QStringLiteral("cancelled"));
    const auto value = model->rowAt(count_row)->value;
    QVERIFY(value.has_value());
    QVERIFY(*value == 3 || *value == 0);
  }
  // A later Refresh without cancel publishes the verified ready rows again.
  inspectorButton(widgets, "statisticsRefresh")->click();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 3, 10000);
  count_row = rowById(model, "chunks.count");
  QVERIFY(count_row >= 0);
  QCOMPARE(model->rowAt(count_row)->value, std::optional<std::uint64_t>{3});
}

void StatisticsControllerTest::exportMatchesSharedSerializer() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  // JSON export: GUI bytes equal the shared serializer bytes exactly.
  const QString json_path = dir.filePath(QStringLiteral("statistics.json"));
  controller.setSavePathCallback([json_path] { return json_path; });
  inspectorButton(widgets, "statisticsExportJson")->click();
  QFile json_file(json_path);
  QVERIFY(json_file.open(QIODevice::ReadOnly));
  const QByteArray json_bytes = json_file.readAll();
  json_file.close();
  const auto reference = collectReference(png.fileName());
  const auto expected_json = pnga::statistics::serialize_statistics_json(
      reference.document, reference.snapshot);
  QVERIFY(expected_json.success);
  QCOMPARE(json_bytes,
           QByteArray(expected_json.bytes.data(),
                      static_cast<int>(expected_json.bytes.size())));

  // CSV export: same byte equality contract.
  const QString csv_path = dir.filePath(QStringLiteral("statistics.csv"));
  controller.setSavePathCallback([csv_path] { return csv_path; });
  inspectorButton(widgets, "statisticsExportCsv")->click();
  QFile csv_file(csv_path);
  QVERIFY(csv_file.open(QIODevice::ReadOnly));
  const QByteArray csv_bytes = csv_file.readAll();
  csv_file.close();
  const auto expected_csv = pnga::statistics::serialize_statistics_csv(
      reference.document, reference.snapshot);
  QVERIFY(expected_csv.success);
  QCOMPARE(csv_bytes,
           QByteArray(expected_csv.bytes.data(),
                      static_cast<int>(expected_csv.bytes.size())));

  // The success copy names the export and carries no partial label here.
  QVERIFY(progressLabel(widgets)->text().contains(QStringLiteral("Exported")));
  QVERIFY(!progressLabel(widgets)->text().contains(
      QStringLiteral("partial"), Qt::CaseInsensitive));
}

void StatisticsControllerTest::partialExportRemainsEnabledAndLabeled() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png, /*truncate=*/true));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);

  // Partial statistics stay exportable and the copy labels them.
  QVERIFY(inspectorButton(widgets, "statisticsExportJson")->isEnabled());
  QVERIFY(progressLabel(widgets)->text().contains(
      QStringLiteral("Partial")));

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString csv_path = dir.filePath(QStringLiteral("partial.csv"));
  controller.setSavePathCallback([csv_path] { return csv_path; });
  inspectorButton(widgets, "statisticsExportCsv")->click();
  QFile csv_file(csv_path);
  QVERIFY(csv_file.open(QIODevice::ReadOnly));
  const QByteArray csv_bytes = csv_file.readAll();
  csv_file.close();
  const auto reference = collectReference(png.fileName());
  const auto expected_csv = pnga::statistics::serialize_statistics_csv(
      reference.document, reference.snapshot);
  QVERIFY(expected_csv.success);
  QCOMPARE(csv_bytes,
           QByteArray(expected_csv.bytes.data(),
                      static_cast<int>(expected_csv.bytes.size())));
  QVERIFY(progressLabel(widgets)->text().contains(
      QStringLiteral("partial"), Qt::CaseInsensitive));
}

void StatisticsControllerTest::exportFailureLeavesTargetUntouched() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);

  // A pre-existing target in a directory where QSaveFile cannot stage its
  // temporary file forces the write failure path: the old bytes must be
  // untouched and a stable error shown.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString target = dir.filePath(QStringLiteral("target.json"));
  {
    QFile old_target(target);
    QVERIFY(old_target.open(QIODevice::WriteOnly));
    QCOMPARE(old_target.write("OLD-BYTES"), qint64(9));
  }
  std::filesystem::permissions(dir.path().toStdString(),
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_exec,
                               std::filesystem::perm_options::replace);
  controller.setSavePathCallback([target] { return target; });
  inspectorButton(widgets, "statisticsExportJson")->click();
  std::filesystem::permissions(dir.path().toStdString(),
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);

  QFile untouched(target);
  QVERIFY(untouched.open(QIODevice::ReadOnly));
  QCOMPARE(untouched.readAll(), QByteArray("OLD-BYTES"));
  untouched.close();
  QVERIFY(progressLabel(widgets)->text().contains(
      QStringLiteral("Export failed")));
}

void StatisticsControllerTest::
    occurrencePublishesOnceAndStalePublishesNothing() {
  QTemporaryFile png;
  QVERIFY(writeFixture(png));

  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  DocumentSession session(&window);
  StatisticsController controller(widgets, session, &window);
  BusRecorder recorder(&window);
  QObject::connect(widgets.bus,
                   &pnga::ui::qt::SelectionBus::selectionChanged, &recorder,
                   &BusRecorder::onSelectionChanged);
  QSignalSpy finished(&session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  QVERIFY(session.replace(png.fileName()));
  widgets.bus->setDocumentGeneration(session.generation());
  session.startPrimaryWorkers();
  widgets.inspector_tabs->setCurrentWidget(widgets.statistics_inspector);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);

  // Select the chunks.IDAT navigation row on the Chunks page and request its
  // first occurrence: exactly one accepted publication through the bus.
  widgets.statistics_inspector->findChild<QTabWidget*>(
             QStringLiteral("statisticsPages"))
      ->setCurrentIndex(1);
  auto* model = chunksModel(widgets);
  const int idat_row = rowById(model, "chunks.IDAT");
  QVERIFY(idat_row >= 0);
  widgets.statistics_inspector
      ->findChild<QTableView*>(QStringLiteral("statisticsChunksTable"))
      ->selectionModel()
      ->select(model->index(idat_row, 0),
               QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  inspectorButton(widgets, "statisticsShowOccurrence")->click();
  QTRY_VERIFY_WITH_TIMEOUT(recorder.count == 1, 10000);
  QCOMPARE(recorder.last_origin, 4);
  QCOMPARE(recorder.last.stage, pnga::trace_model::Stage::kChunk);
  QVERIFY(!recorder.last.physical_spans.empty());

  // A stale occurrence result publishes nothing: request another occurrence
  // and replace the document before it can publish.
  inspectorButton(widgets, "statisticsShowOccurrence")->click();
  QVERIFY(session.replace(png.fileName()));
  QTest::qWait(300);
  QCOMPARE(recorder.count, 1);
}

QTEST_MAIN(StatisticsControllerTest)
#include "statistics_controller_test.moc"
