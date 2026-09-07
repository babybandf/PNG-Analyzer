// WP-602G: Statistics model/inspector tests (ruling R11). The Statistics
// page is a four-page QTabWidget (Overview | Chunks | Filters | DEFLATE)
// whose tables are QTableViews driven by StatisticsTableModel over the
// immutable Task 6 rows — no QTableWidget, no per-row widgets. The GUI
// formats values only in the display role (current QLocale) while the raw
// quint64 stays on the user role; navigation rows enable Show occurrence and
// Enter triggers it; Escape cancels; the action and page-table tab order is
// complete; 320 px width scrolls inside the tables instead of growing.

#include <pnga/analysis-engine/statistics_view.h>
#include <pnga/statistics/statistics.h>
#include <pnga/ui/qt/statistics_inspector.h>
#include <pnga/ui/qt/statistics_table_model.h>

#include <QtTest/QtTest>

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableView>
#include <QTabWidget>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using pnga::analysis_engine::StatisticsView;
using pnga::analysis_engine::build_statistics_view;
using pnga::statistics::BlockKind;
using pnga::statistics::SectionScope;
using pnga::statistics::SectionStatus;
using pnga::statistics::StatisticsAccumulator;
using pnga::statistics::StatisticsSnapshot;
using pnga::statistics::TokenKind;

pnga::statistics::StatisticsSnapshot readySnapshot() {
  StatisticsAccumulator accumulator;
  accumulator.set_compression_totals(1234, 5678);
  accumulator.add(pnga::statistics::ChunkSample{"IHDR", 13});
  accumulator.add(pnga::statistics::ChunkSample{"IDAT", 100});
  accumulator.add(pnga::statistics::FilterSample{0, 10});
  accumulator.add(pnga::statistics::FilterSample{1, 12});
  accumulator.add(pnga::statistics::BlockSample{BlockKind::kFixed, 24, 12});
  accumulator.add(pnga::statistics::TokenSample{TokenKind::kLiteral, 8, 1, 0, 0});
  accumulator.add(
      pnga::statistics::TokenSample{TokenKind::kLengthDistance, 12, 3, 3, 1});
  accumulator.add(pnga::statistics::TokenSample{TokenKind::kEndOfBlock, 7, 0, 0, 0});
  const auto finish_all = [&accumulator](SectionStatus status, bool complete,
                                         SectionScope scope) {
    for (const pnga::statistics::StatisticsSectionId id :
         {pnga::statistics::StatisticsSectionId::kOverview,
          pnga::statistics::StatisticsSectionId::kChunks,
          pnga::statistics::StatisticsSectionId::kFilters,
          pnga::statistics::StatisticsSectionId::kBlocks,
          pnga::statistics::StatisticsSectionId::kTokens,
          pnga::statistics::StatisticsSectionId::kLengths,
          pnga::statistics::StatisticsSectionId::kDistances}) {
      accumulator.finish(id, status, complete, scope);
    }
  };
  finish_all(SectionStatus::kReady, true, SectionScope::kWholeDocument);
  return accumulator.snapshot();
}

pnga::statistics::StatisticsSnapshot partialSnapshot() {
  StatisticsAccumulator accumulator;
  accumulator.add(pnga::statistics::ChunkSample{"IDAT", 64});
  accumulator.finish(pnga::statistics::StatisticsSectionId::kOverview,
                     SectionStatus::kPartial, false,
                     SectionScope::kVerifiedPrefix);
  accumulator.finish(pnga::statistics::StatisticsSectionId::kChunks,
                     SectionStatus::kReady, true,
                     SectionScope::kWholeDocument);
  for (const pnga::statistics::StatisticsSectionId id :
       {pnga::statistics::StatisticsSectionId::kFilters,
        pnga::statistics::StatisticsSectionId::kBlocks,
        pnga::statistics::StatisticsSectionId::kTokens,
        pnga::statistics::StatisticsSectionId::kLengths,
        pnga::statistics::StatisticsSectionId::kDistances}) {
    accumulator.finish(id, SectionStatus::kPartial, false,
                       SectionScope::kVerifiedPrefix);
  }
  return accumulator.snapshot();
}

std::shared_ptr<const StatisticsView> viewFor(std::uint64_t generation,
                                              const StatisticsSnapshot& snapshot) {
  return std::make_shared<const StatisticsView>(
      build_statistics_view(generation, snapshot));
}

std::shared_ptr<const StatisticsView> readyView(std::uint64_t generation) {
  return viewFor(generation, readySnapshot());
}

std::shared_ptr<const StatisticsView> partialView(std::uint64_t generation) {
  return viewFor(generation, partialSnapshot());
}

bool selectRowById(pnga::ui::qt::StatisticsTableModel* model,
                   QTableView* table, const QString& id) {
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto* entry = model->rowAt(row);
    if (entry != nullptr && id == QString::fromStdString(entry->id)) {
      table->selectionModel()->select(
          model->index(row, 0),
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      return true;
    }
  }
  return false;
}

}  // namespace

class StatisticsInspectorTest : public QObject {
  Q_OBJECT
 private slots:
  void constructionContract();
  void unavailableViewShowsNoZeroValues();
  void localeDisplayOnlyInDisplayRole();
  void readyThenPartialPublishVerifiedRows();
  void selectionPreservedByStableRowId();
  void occurrenceActionContract();
  void actionLayoutMatchesToolbarContract();
  void actionSignalsFireOnceAndEscapeCancels();
  void keyboardTabOrderCoversActionsAndTables();
  void narrowWidthKeepsTablesScrollable();
  void columnRefitPolicyPerGeneration();

 private:
  static std::vector<QTableView*> pageTables(QWidget& inspector);
};

std::vector<QTableView*> StatisticsInspectorTest::pageTables(
    QWidget& inspector) {
  std::vector<QTableView*> tables;
  for (const char* const name :
       {"statisticsOverviewTable", "statisticsChunksTable",
        "statisticsFiltersTable", "statisticsDeflateTable"}) {
    auto* table = inspector.findChild<QTableView*>(QString::fromLatin1(name));
    if (table != nullptr) {
      tables.push_back(table);
    }
  }
  return tables;
}

void StatisticsInspectorTest::constructionContract() {
  pnga::ui::qt::StatisticsInspector inspector;

  // Frozen object names (WP-602G task package).
  QCOMPARE(inspector.objectName(), QStringLiteral("statisticsInspector"));
  QVERIFY(inspector.findChild<QTabWidget*>(QStringLiteral("statisticsPages")) !=
          nullptr);
  for (const char* const name :
       {"statisticsOverviewTable", "statisticsChunksTable",
        "statisticsFiltersTable", "statisticsDeflateTable",
        "statisticsRefresh", "statisticsCancel", "statisticsExportJson",
        "statisticsExportCsv", "statisticsShowOccurrence",
        "statisticsProgress"}) {
    QVERIFY2(inspector.findChild<QObject*>(QString::fromLatin1(name)) != nullptr,
             name);
  }

  // Non-empty accessible names on every named object.
  QVERIFY(!inspector.accessibleName().isEmpty());
  const auto named = inspector.findChildren<QObject*>();
  for (const QObject* object : named) {
    const QString name = object->objectName();
    if (name.startsWith(QStringLiteral("statistics"))) {
      const QWidget* widget = qobject_cast<const QWidget*>(object);
      if (widget != nullptr) {
        QVERIFY2(!widget->accessibleName().isEmpty(),
             qPrintable(name));
      }
    }
  }

  auto* occurrence = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsShowOccurrence"));
  QVERIFY(occurrence != nullptr);
  QCOMPARE(occurrence->text(), QStringLiteral("Show in Hex"));
  QCOMPARE(occurrence->accessibleName(),
           QStringLiteral("Show selected occurrence in Hex"));

  // Page labels and their fixed order (R11): Overview | Chunks | Filters |
  // DEFLATE, and no fifth inner page.
  auto* pages =
      inspector.findChild<QTabWidget*>(QStringLiteral("statisticsPages"));
  QVERIFY(pages != nullptr);
  QCOMPARE(pages->count(), 4);
  QCOMPARE(pages->tabText(0), QStringLiteral("Overview"));
  QCOMPARE(pages->tabText(1), QStringLiteral("Chunks"));
  QCOMPARE(pages->tabText(2), QStringLiteral("Filters"));
  QCOMPARE(pages->tabText(3), QStringLiteral("DEFLATE"));

  // Model-backed tables only: no QTableWidget and no per-row widgets.
  QVERIFY(inspector.findChildren<QTableWidget*>().isEmpty());
  QCOMPARE(pageTables(inspector).size(), std::size_t{4});

  // Initial unavailable copy without zero values: the progress label carries
  // an instruction and no collected digit, the tables are empty.
  auto* progress = inspector.findChild<QLabel*>(QStringLiteral("statisticsProgress"));
  QVERIFY(progress != nullptr);
  QVERIFY(!progress->text().isEmpty());
  QVERIFY(std::none_of(progress->text().cbegin(), progress->text().cend(),
                       [](QChar c) { return c.isDigit(); }));
  for (QTableView* table : pageTables(inspector)) {
    QVERIFY(table->model() != nullptr);
    QCOMPARE(table->model()->rowCount(), 0);
  }
}

void StatisticsInspectorTest::unavailableViewShowsNoZeroValues() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(viewFor(1, StatisticsSnapshot{}));

  auto* pages =
      inspector.findChild<QTabWidget*>(QStringLiteral("statisticsPages"));
  const auto tables = pageTables(inspector);
  for (int page = 0; page < pages->count(); ++page) {
    QTableView* table = tables[static_cast<std::size_t>(page)];
    auto* model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
        table->model());
    QVERIFY(model->rowCount() > 0);
    for (int row = 0; row < model->rowCount(); ++row) {
      const QModelIndex value = model->index(row, pnga::ui::qt::StatisticsTableModel::Value);
      const QModelIndex status = model->index(row, pnga::ui::qt::StatisticsTableModel::Status);
      // Missing data is unavailable, never a ready zero value.
      QCOMPARE(model->data(value, Qt::DisplayRole).toString(),
               QStringLiteral("\u2014"));
      QVERIFY(!model->data(value, pnga::ui::qt::RawValueRole).isValid());
      QCOMPARE(model->data(status, Qt::DisplayRole).toString(),
               QStringLiteral("unavailable"));
    }
    // The Group column only matters on the grouped DEFLATE page.
    QCOMPARE(table->isColumnHidden(pnga::ui::qt::StatisticsTableModel::Group),
             page != 3);
  }
}

void StatisticsInspectorTest::localeDisplayOnlyInDisplayRole() {
  const QLocale previous = QLocale();
  QLocale::setDefault(QLocale::German);

  pnga::ui::qt::StatisticsTableModel model;
  std::vector<pnga::analysis_engine::StatisticsRow> row_vector;
  row_vector.push_back([] {
    pnga::analysis_engine::StatisticsRow row;
    row.id = "chunks.count";
    row.group = "chunks";
    row.label = "Chunks";
    row.value = std::uint64_t{1000};
    row.unit = "chunks";
    return row;
  }());
  auto rows = std::make_shared<const std::vector<pnga::analysis_engine::StatisticsRow>>(
      std::move(row_vector));
  model.setRows(rows);

  const QModelIndex value = model.index(0, pnga::ui::qt::StatisticsTableModel::Value);
  // The display role formats through the current QLocale (German groups with
  // a dot); the user role keeps the raw quint64.
  QCOMPARE(model.data(value, Qt::DisplayRole).toString(), QStringLiteral("1.000"));
  QCOMPARE(model.data(value, pnga::ui::qt::RawValueRole).value<quint64>(),
           std::uint64_t{1000});

  QLocale::setDefault(previous);
}

void StatisticsInspectorTest::readyThenPartialPublishVerifiedRows() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(readyView(7));

  const auto tables = pageTables(inspector);
  auto* chunks_model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
      tables[1]->model());
  QVERIFY(chunks_model->rowCount() > 2);
  int idat_row = -1;
  for (int row = 0; row < chunks_model->rowCount(); ++row) {
    if (chunks_model->rowAt(row)->id == "chunks.IDAT") {
      idat_row = row;
    }
  }
  QVERIFY(idat_row >= 0);
  // Bucket rows carry the bucket count (one IDAT chunk here).
  QCOMPARE(chunks_model->rowAt(idat_row)->value, std::optional<std::uint64_t>{1});
  QCOMPARE(chunks_model->data(chunks_model->index(idat_row, pnga::ui::qt::StatisticsTableModel::Status),
                              Qt::DisplayRole)
               .toString(),
           QStringLiteral("ready"));

  // A later partial publication keeps every verified row and its raw value;
  // partial sections keep their collected evidence and status.
  inspector.setView(partialView(7));
  int idat_after = -1;
  for (int row = 0; row < chunks_model->rowCount(); ++row) {
    if (chunks_model->rowAt(row)->id == "chunks.IDAT") {
      idat_after = row;
    }
  }
  QVERIFY(idat_after >= 0);
  QCOMPARE(chunks_model->rowAt(idat_after)->value, std::optional<std::uint64_t>{1});
  QCOMPARE(chunks_model->data(chunks_model->index(idat_after, pnga::ui::qt::StatisticsTableModel::Status),
                              Qt::DisplayRole)
               .toString(),
           QStringLiteral("ready"));
  auto* deflate_model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
      tables[3]->model());
  QVERIFY(deflate_model->rowCount() > 0);
  for (int row = 0; row < deflate_model->rowCount(); ++row) {
    QCOMPARE(deflate_model->data(deflate_model->index(row, pnga::ui::qt::StatisticsTableModel::Status),
                                 Qt::DisplayRole)
                 .toString(),
             QStringLiteral("partial"));
  }
}

void StatisticsInspectorTest::selectionPreservedByStableRowId() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(readyView(7));
  const auto tables = pageTables(inspector);
  auto* chunks_model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
      tables[1]->model());
  QVERIFY(selectRowById(chunks_model, tables[1], QStringLiteral("chunks.IDAT")));
  QCOMPARE(tables[1]->selectionModel()->selectedRows().size(), 1);

  // Same-generation republish keeps the selection by stable row id.
  inspector.setView(readyView(7));
  QCOMPARE(tables[1]->selectionModel()->selectedRows().size(), 1);
  QCOMPARE(chunks_model->rowAt(tables[1]->selectionModel()->selectedRows()
                                   .front()
                                   .row())
               ->id,
           "chunks.IDAT");

  // A new generation publishing the same row keeps the selection too.
  inspector.setView(readyView(8));
  QCOMPARE(tables[1]->selectionModel()->selectedRows().size(), 1);
  QCOMPARE(chunks_model->rowAt(tables[1]->selectionModel()->selectedRows()
                                   .front()
                                   .row())
               ->id,
           "chunks.IDAT");
}

void StatisticsInspectorTest::occurrenceActionContract() {
  qRegisterMetaType<pnga::analysis_engine::StatisticsNavigationRequest>();
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(readyView(7));

  auto* occurrence = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsShowOccurrence"));
  QVERIFY(occurrence != nullptr);
  QSignalSpy requests(&inspector,
                      &pnga::ui::qt::StatisticsInspector::occurrenceRequested);
  QVERIFY(requests.isValid());

  const auto tables = pageTables(inspector);
  auto* chunks_model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
      tables[1]->model());
  QVERIFY(selectRowById(chunks_model, tables[1], QStringLiteral("chunks.IDAT")));
  inspector.findChild<QTabWidget*>(QStringLiteral("statisticsPages"))
      ->setCurrentIndex(1);
  // A navigation row enables Show occurrence; clicking fires the typed
  // request from the row, exactly once.
  QVERIFY(occurrence->isEnabled());
  occurrence->click();
  QCOMPARE(requests.count(), 1);
  {
    const auto request =
        requests.front().front().value<pnga::analysis_engine::StatisticsNavigationRequest>();
    QCOMPARE(request.generation, std::uint64_t{7});
    QCOMPARE(request.domain,
             pnga::analysis_engine::StatisticsBucketDomain::kChunkType);
    QCOMPARE(QString::fromStdString(request.key), QStringLiteral("IDAT"));
    QCOMPARE(request.direction,
             pnga::analysis_engine::OccurrenceDirection::kFirst);
    QCOMPARE(request.max_tokens,
             pnga::analysis_engine::kOccurrenceMaxTokens);
    QCOMPARE(request.max_input_bytes,
             pnga::analysis_engine::kOccurrenceMaxInputBytes);
  }

  // A plain total row offers no navigation and disables the action.
  QVERIFY(selectRowById(chunks_model, tables[1], QStringLiteral("chunks.count")));
  QVERIFY(!occurrence->isEnabled());
  occurrence->click();
  QCOMPARE(requests.count(), 1);

  // Enter on the table triggers the occurrence request for the current row.
  QVERIFY(selectRowById(chunks_model, tables[1], QStringLiteral("chunks.IHDR")));
  QVERIFY(occurrence->isEnabled());
  tables[1]->setFocus();
  QTest::keyClick(tables[1], Qt::Key_Return);
  QCOMPARE(requests.count(), 2);
}

void StatisticsInspectorTest::actionLayoutMatchesToolbarContract() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.resize(480, 400);
  inspector.show();
  QVERIFY(QTest::qWaitForWindowExposed(&inspector));
  QTest::qWait(50);

  auto* refresh = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsRefresh"));
  auto* occurrence = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsShowOccurrence"));
  auto* cancel = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsCancel"));
  auto* export_json = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsExportJson"));
  auto* export_csv = inspector.findChild<QPushButton*>(
      QStringLiteral("statisticsExportCsv"));
  QVERIFY(refresh != nullptr && occurrence != nullptr && cancel != nullptr &&
          export_json != nullptr && export_csv != nullptr);

  QCOMPARE(refresh->y(), occurrence->y());
  QCOMPARE(occurrence->y(), cancel->y());
  QVERIFY(refresh->x() < occurrence->x());
  QVERIFY(occurrence->x() < cancel->x());
  QCOMPARE(export_json->width(), export_csv->width());
  QCOMPARE(export_json->y(), export_csv->y());
  QVERIFY(refresh->y() < export_json->y());
}

void StatisticsInspectorTest::actionSignalsFireOnceAndEscapeCancels() {
  pnga::ui::qt::StatisticsInspector inspector;
  // A published view with a usable section enables the export actions.
  inspector.setView(readyView(7));
  QSignalSpy refresh(&inspector, &pnga::ui::qt::StatisticsInspector::refreshRequested);
  QSignalSpy cancel(&inspector, &pnga::ui::qt::StatisticsInspector::cancelRequested);
  QSignalSpy export_json(
      &inspector, &pnga::ui::qt::StatisticsInspector::exportRequested);
  QSignalSpy occurrence(
      &inspector, &pnga::ui::qt::StatisticsInspector::occurrenceRequested);
  QVERIFY(refresh.isValid() && cancel.isValid() && export_json.isValid() &&
          occurrence.isValid());

  inspector.findChild<QPushButton*>(QStringLiteral("statisticsRefresh"))->click();
  inspector.findChild<QPushButton*>(QStringLiteral("statisticsCancel"))->click();
  inspector.findChild<QPushButton*>(QStringLiteral("statisticsExportJson"))->click();
  inspector.findChild<QPushButton*>(QStringLiteral("statisticsExportCsv"))->click();
  QCOMPARE(refresh.count(), 1);
  QCOMPARE(cancel.count(), 1);
  // Both export actions share the typed signal; each fires exactly once.
  QCOMPARE(export_json.count(), 2);
  QCOMPARE(export_json.front().front().toInt(),
           static_cast<int>(pnga::ui::qt::StatisticsExportFormat::kJson));
  QCOMPARE(export_json.back().front().toInt(),
           static_cast<int>(pnga::ui::qt::StatisticsExportFormat::kCsv));
  QCOMPARE(occurrence.count(), 0);

  // Escape anywhere in the inspector cancels.
  QTest::keyClick(&inspector, Qt::Key_Escape);
  QCOMPARE(cancel.count(), 2);
}

void StatisticsInspectorTest::keyboardTabOrderCoversActionsAndTables() {
  pnga::ui::qt::StatisticsInspector inspector;
  // Publish a view, select a navigation row and make its page current so
  // every action (including Show occurrence) participates in the chain.
  inspector.setView(readyView(7));
  const auto tables = pageTables(inspector);
  auto* chunks_model = static_cast<pnga::ui::qt::StatisticsTableModel*>(
      tables[1]->model());
  QVERIFY(selectRowById(chunks_model, tables[1], QStringLiteral("chunks.IDAT")));
  inspector.findChild<QTabWidget*>(QStringLiteral("statisticsPages"))
      ->setCurrentIndex(1);

  inspector.show();
  QVERIFY(QTest::qWaitForWindowExposed(&inspector));

  auto* refresh =
      inspector.findChild<QPushButton*>(QStringLiteral("statisticsRefresh"));
  refresh->setFocus();
  QVERIFY(refresh->hasFocus());
  QVERIFY(inspector.findChild<QPushButton*>(
              QStringLiteral("statisticsShowOccurrence"))
              ->isEnabled());

  const QStringList expected{
      QStringLiteral("statisticsShowOccurrence"),
      QStringLiteral("statisticsCancel"), QStringLiteral("statisticsExportJson"),
      QStringLiteral("statisticsExportCsv"), QStringLiteral("statisticsChunksTable")};
  for (const QString& expected_name : expected) {
    QTest::keyClick(&inspector, Qt::Key_Tab);
    const QString reached = inspector.focusWidget() == nullptr
                                ? QString()
                                : inspector.focusWidget()->objectName();
    QVERIFY2(reached == expected_name,
             qPrintable(QStringLiteral("expected %1 but reached %2")
                            .arg(expected_name, reached)));
  }
}

void StatisticsInspectorTest::narrowWidthKeepsTablesScrollable() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(readyView(7));
  inspector.resize(320, 400);
  inspector.show();
  QVERIFY(QTest::qWaitForWindowExposed(&inspector));
  QTest::qWait(50);

  // At 320 px the tables never drive the page wider: they scroll horizontally
  // inside the viewport instead.
  for (QTableView* table : pageTables(inspector)) {
    QVERIFY(table->width() > 0);
    QVERIFY(table->width() <= 320);
    QCOMPARE(table->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(table->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  }
}

void StatisticsInspectorTest::columnRefitPolicyPerGeneration() {
  pnga::ui::qt::StatisticsInspector inspector;
  inspector.setView(readyView(7));
  const auto tables = pageTables(inspector);
  QTableView* table = tables[1];
  auto* header = table->horizontalHeader();

  // Same-generation republish preserves manual widths (user resize wins).
  const int manual = header->sectionSize(pnga::ui::qt::StatisticsTableModel::Metric) + 37;
  table->setColumnWidth(pnga::ui::qt::StatisticsTableModel::Metric, manual);
  inspector.setView(readyView(7));
  QCOMPARE(header->sectionSize(pnga::ui::qt::StatisticsTableModel::Metric), manual);

  // A generation change re-derives content widths (fresh open contract).
  inspector.setView(readyView(8));
  const int refit = header->sectionSize(pnga::ui::qt::StatisticsTableModel::Metric);
  table->resizeColumnsToContents();
  QCOMPARE(header->sectionSize(pnga::ui::qt::StatisticsTableModel::Metric), refit);
}

QTEST_MAIN(StatisticsInspectorTest)
#include "statistics_inspector_test.moc"
