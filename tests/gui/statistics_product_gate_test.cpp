// WP-602H product gate: drives the real MainWindow over the WP-607C corpus
// and closes the frozen Statistics product matrix (ruling R13): lazy start
// and open/hover non-regression, ready/malformed/large/rapid-switch/
// cancel/stale behavior, honest per-section statuses, three locale display
// passes with byte-identical exports, GUI == shared serializer == CLI byte
// equality, strict CSV importability, 320 px Inspector width, keyboard and
// accessibility contracts and the bounded O(1) token retention / 64 MiB
// working-memory invariants. Statistics work must never start on file open
// or hover; no threshold, budget or fixture is weakened or skipped.

#include "document_session.h"
#include "main_window.h"

#include <pnga/analysis-engine/statistics_collector.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/deflate-trace/token_decoder.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/statistics/serialization.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/selection_bus.h>
#include <pnga/ui/qt/statistics_inspector.h>
#include <pnga/ui/qt/statistics_table_model.h>

#include <QtTest/QtTest>

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef PNGA_WP607C_CORPUS_DIR
#error "PNGA_WP607C_CORPUS_DIR must be defined by the build"
#endif
#ifndef PNGA_CLI_PATH
#error "PNGA_CLI_PATH must be defined by the build"
#endif

namespace {

using pnga::analysis_engine::StatisticsCollectionResult;
using pnga::ui::qt::StatisticsInspector;
using pnga::ui::qt::StatisticsTableModel;

constexpr int kTimeoutMs = 30000;
constexpr int kLargeTimeoutMs = 180000;
constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 760;
constexpr int kNarrowWidth = 320;
constexpr std::uint64_t kMaxWorkingBytes = 64ull << 20;

QString fixture_path(const char* relative) {
  return QDir(QString::fromUtf8(PNGA_WP607C_CORPUS_DIR))
      .filePath(QString::fromLatin1(relative));
}

DocumentSession* session_of(const MainWindow& window) {
  return window.findChild<DocumentSession*>();
}

StatisticsInspector* statistics_inspector(const MainWindow& window) {
  return window.findChild<StatisticsInspector*>(
      QStringLiteral("statisticsInspector"));
}

QTabWidget* inspector_tabs(const MainWindow& window) {
  return window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
}

void select_statistics_tab(MainWindow& window) {
  QTabWidget* tabs = inspector_tabs(window);
  QVERIFY(tabs != nullptr);
  tabs->setCurrentWidget(statistics_inspector(window));
}

QTableView* statistics_table(const MainWindow& window, const char* name) {
  return statistics_inspector(window)
      ->findChild<QTableView*>(QString::fromLatin1(name));
}

StatisticsTableModel* table_model(const MainWindow& window, const char* name) {
  return static_cast<StatisticsTableModel*>(
      statistics_table(window, name)->model());
}

QLabel* progress_label(const MainWindow& window) {
  return statistics_inspector(window)
      ->findChild<QLabel*>(QStringLiteral("statisticsProgress"));
}

QPushButton* statistics_button(const MainWindow& window, const char* name) {
  return statistics_inspector(window)
      ->findChild<QPushButton*>(QString::fromLatin1(name));
}

int row_by_id(StatisticsTableModel* model, const char* id) {
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->rowAt(row)->id == id) {
      return row;
    }
  }
  return -1;
}

// A live document with a decoded image.
void open_and_wait_ready(MainWindow& window, const char* relative) {
  QVERIFY(window.openFile(fixture_path(relative)));
  QCoreApplication::processEvents();
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), kTimeoutMs);
}

// Synchronous reference collection through the shared pipeline: the same
// source/index/StageSet inputs the DocumentSession worker uses.
StatisticsCollectionResult collect_reference(const QString& path) {
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
  request.max_working_bytes = kMaxWorkingBytes;
  return pnga::analysis_engine::collect_document_statistics(request, nullptr,
                                                            {});
}

// Runs the real CLI binary and returns its stdout report bytes.
void run_cli(const QString& file, const char* format, int* exit_code,
             QByteArray* stdout_bytes) {
  QProcess process;
  process.start(QString::fromUtf8(PNGA_CLI_PATH),
                {QStringLiteral("statistics"), file,
                 QStringLiteral("--format"), QString::fromLatin1(format)});
  QVERIFY(process.waitForStarted(kTimeoutMs));
  QVERIFY(process.waitForFinished(kLargeTimeoutMs));
  *exit_code = process.exitCode();
  *stdout_bytes = process.readAllStandardOutput();
}

void read_file(const QString& path, QByteArray* out) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  *out = file.readAll();
}

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  Q_ASSERT(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

void exported_sections(const QByteArray& json_bytes, QJsonObject* sections) {
  const QJsonDocument document = QJsonDocument::fromJson(json_bytes);
  QVERIFY(!document.isNull());
  const QJsonObject root = document.object();
  QCOMPARE(root.value(QStringLiteral("schema")).toString(),
           QStringLiteral("pnga.statistics"));
  QCOMPARE(root.value(QStringLiteral("schema_version")).toInt(), 1);
  *sections = root.value(QStringLiteral("sections")).toObject();
  QCOMPARE(sections->size(), 7);
}

// Honest-section contract (rulings R2/R3): every section carries status,
// complete and scope; only ready+complete+whole_document claims
// completeness; a ready section is never contradictory.
void assert_honest_sections(const QJsonObject& sections,
                            bool* saw_not_ready = nullptr) {
  static const char* kSectionIds[] = {
      "overview", "chunks", "filters", "blocks",
      "tokens",   "lengths", "distances"};
  for (const char* id : kSectionIds) {
    QVERIFY2(sections.contains(QString::fromLatin1(id)), id);
    const QJsonObject section =
        sections.value(QString::fromLatin1(id)).toObject();
    QVERIFY2(section.contains(QStringLiteral("status")), id);
    QVERIFY2(section.contains(QStringLiteral("complete")), id);
    QVERIFY2(section.contains(QStringLiteral("scope")), id);
    const QString status =
        section.value(QStringLiteral("status")).toString();
    const bool complete = section.value(QStringLiteral("complete")).toBool();
    const QString scope = section.value(QStringLiteral("scope")).toString();
    if (status == QStringLiteral("ready")) {
      QVERIFY2(complete, id);
      QCOMPARE(scope, QStringLiteral("whole_document"));
    } else {
      QVERIFY2(!complete, id);
      if (saw_not_ready != nullptr) {
        *saw_not_ready = true;
      }
    }
  }
}

// The exported report must carry no path, timestamp or clock artifacts.
void assert_no_path_or_clock_artifacts(const QByteArray& bytes) {
  QVERIFY(!bytes.contains("/Users/"));
  QVERIFY(!bytes.contains("/tmp/"));
  QVERIFY(!bytes.contains("C:\\"));
  QVERIFY(!bytes.contains("timestamp"));
  QVERIFY(!bytes.contains("generated_at"));
  QVERIFY(!bytes.contains("mtime"));
}

// Strict RFC 4180 importability parser: UTF-8 without BOM, LF only, exactly
// one final LF, the frozen six-column header and six fields per record,
// decimal ASCII (or empty/boolean/word) values only.
bool parse_csv_strict(const QByteArray& bytes, QVector<QStringList>* records,
                      QString* error) {
  if (bytes.startsWith("\xEF\xBB\xBF")) {
    *error = QStringLiteral("CSV starts with a BOM");
    return false;
  }
  if (bytes.contains('\r')) {
    *error = QStringLiteral("CSV contains a CR byte");
    return false;
  }
  if (!bytes.endsWith('\n')) {
    *error = QStringLiteral("CSV does not end with LF");
    return false;
  }
  if (bytes.size() >= 2 && bytes.at(bytes.size() - 2) == '\n') {
    *error = QStringLiteral("CSV ends with a blank line");
    return false;
  }
  const QList<QByteArray> lines = bytes.split('\n');
  records->clear();
  for (int line_index = 0; line_index < lines.size(); ++line_index) {
    const QByteArray& line = lines.at(line_index);
    if (line.isEmpty() && line_index == lines.size() - 1) {
      break;  // the single final LF
    }
    QStringList fields;
    QString field;
    bool in_quotes = false;
    for (int i = 0; i < line.size(); ++i) {
      const char ch = line.at(i);
      if (in_quotes) {
        if (ch == '"') {
          if (i + 1 < line.size() && line.at(i + 1) == '"') {
            field += QLatin1Char('"');
            ++i;
          } else {
            in_quotes = false;
          }
        } else {
          field += QLatin1Char(ch);
        }
      } else if (ch == '"') {
        if (!field.isEmpty()) {
          *error = QStringLiteral("quote inside unquoted field at line %1")
                       .arg(line_index + 1);
          return false;
        }
        in_quotes = true;
      } else if (ch == ',') {
        fields.append(field);
        field.clear();
      } else {
        field += QLatin1Char(ch);
      }
    }
    if (in_quotes) {
      *error = QStringLiteral("unterminated quote at line %1")
                   .arg(line_index + 1);
      return false;
    }
    fields.append(field);
    if (fields.size() != 6) {
      *error = QStringLiteral("line %1 has %2 fields instead of 6")
                   .arg(line_index + 1)
                   .arg(fields.size());
      return false;
    }
    if (records->isEmpty()) {
      const QStringList header{QStringLiteral("schema_version"),
                               QStringLiteral("section"),
                               QStringLiteral("metric"),
                               QStringLiteral("key"),
                               QStringLiteral("value"),
                               QStringLiteral("unit")};
      if (fields != header) {
        *error = QStringLiteral("CSV header is not the frozen contract");
        return false;
      }
    } else {
      const QString& value = fields.at(4);
      bool decimal_like = value.isEmpty();
      for (int i = 0; i < value.size() && !decimal_like; ++i) {
        decimal_like = value.at(i).isDigit();
      }
      const bool word_like =
          value == QStringLiteral("true") || value == QStringLiteral("false") ||
          value == value.toLower();
      if (!decimal_like && !word_like) {
        *error = QStringLiteral("value %1 at line %2 is not decimal ASCII")
                     .arg(value)
                     .arg(line_index + 1);
        return false;
      }
    }
    records->append(fields);
  }
  return true;
}

void require_importable_csv(const QString& path, QVector<QStringList>* out) {
  QByteArray bytes;
  read_file(path, &bytes);
  QVector<QStringList> records;
  QString error;
  QVERIFY2(parse_csv_strict(bytes, &records, &error),
           qPrintable(QStringLiteral("%1: %2").arg(path, error)));
  QVERIFY(records.size() >= 2);
  *out = records;
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
    if (origin == 4) {  // kStatisticsPanelOrigin
      ++statistics_count;
    }
    last_origin = origin;
    last = selection;
  }

 public:
  int count = 0;
  int statistics_count = 0;
  int last_origin = 0;
  pnga::trace_model::Selection last;
};

// Selects the first row that carries a typed navigation request; returns
// false when the page has none (then Show occurrence stays disabled and the
// focus chain skips it).
bool select_first_navigation_row(StatisticsTableModel* model,
                                 QTableView* table) {
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto* entry = model->rowAt(row);
    if (entry != nullptr && entry->navigation.has_value()) {
      table->selectionModel()->select(
          model->index(row, 0),
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      return true;
    }
  }
  return false;
}

// Adapts the virtual IDAT stream to an IByteSource without any payload copy;
// view() is refused so the scalar scan must operate through bounded read().
class VirtualIdatGateSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatGateSource(const pnga::png_format::VirtualIDATStream& stream,
                        const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const pnga::png_format::VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

}  // namespace

class StatisticsProductGateTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void openAndHoverStartNoStatistics();
  void readyMatrixExportByteEqualityAndCsv();
  void malformedPartialExportHonestSections();
  void largeCollectionBoundedRetention();
  void rapidSwitchStaleGenerationNonPublication();
  void cancelRetainsVerifiedRows();
  void localeDisplayPassesKeepExportBytes();
  void inspector320KeyboardAccessibilityOccurrence();
};

void StatisticsProductGateTest::init() {
  QSettings settings;
  settings.clear();
}

// Lazy start (ruling R7): opening a document and 200 delivered hover events
// start no statistics collection whatsoever.
void StatisticsProductGateTest::openAndHoverStartNoStatistics() {
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));

  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());
  QSignalSpy progress(session, &DocumentSession::statisticsProgress);
  QVERIFY(progress.isValid());

  open_and_wait_ready(window, "valid/ui-gray1-none.png");
  QCoreApplication::processEvents();

  // No statistics worker exists and no result was published on file open.
  QCOMPARE(finished.count(), 0);
  QCOMPARE(progress.count(), 0);
  QVERIFY(session->findChildren<StatisticsWorker*>().isEmpty());
  QCOMPARE(progress_label(window)->text(),
           QStringLiteral("Statistics are collected when this tab is opened."));

  // 200 deterministic hover events over the delivered image view.
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  image->clearHoverPixel();
  QSignalSpy hovered(image, &pnga::ui::qt::DeliveredImageView::pixelHovered);
  QVERIFY(hovered.isValid());
  constexpr int kHoverEvents = 200;
  for (int i = 0; i < kHoverEvents; ++i) {
    const QPoint local(
        2 + (i * 7) % std::max(1, image->width() - 4),
        2 + (i * 11) % std::max(1, image->height() - 4));
    QMouseEvent hover(QEvent::MouseMove, QPointF(local),
                      QPointF(image->mapToGlobal(local)), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(image, &hover);
    if (i % 25 == 0) {
      QCoreApplication::processEvents();
    }
  }
  QCoreApplication::processEvents();
  QVERIFY2(hovered.count() >= 1,
           "the hover path must stay live while statistics stay idle");

  // Still zero statistics activity after the hover storm.
  QCOMPARE(finished.count(), 0);
  QCOMPARE(progress.count(), 0);
  QVERIFY(session->findChildren<StatisticsWorker*>().isEmpty());
  QCOMPARE(progress_label(window)->text(),
           QStringLiteral("Statistics are collected when this tab is opened."));
}

// Ready product pass: four frozen pages, honest ready sections, exports
// byte-equal to the shared serializer and the real CLI, importable CSV.
void StatisticsProductGateTest::readyMatrixExportByteEqualityAndCsv() {
  const char* relative = "valid/ui-gray1-none.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, relative);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kTimeoutMs);

  // Four frozen pages in the fixed order with populated tables.
  auto* pages = statistics_inspector(window)
                    ->findChild<QTabWidget*>(QStringLiteral("statisticsPages"));
  QVERIFY(pages != nullptr);
  QCOMPARE(pages->count(), 4);
  QCOMPARE(pages->tabText(0), QStringLiteral("Overview"));
  QCOMPARE(pages->tabText(1), QStringLiteral("Chunks"));
  QCOMPARE(pages->tabText(2), QStringLiteral("Filters"));
  QCOMPARE(pages->tabText(3), QStringLiteral("DEFLATE"));
  for (const char* name :
       {"statisticsOverviewTable", "statisticsChunksTable",
        "statisticsFiltersTable", "statisticsDeflateTable"}) {
    QVERIFY2(table_model(window, name)->rowCount() > 0, name);
  }

  // The chunks table carries the verified, raw integer row.
  auto* chunks = table_model(window, "statisticsChunksTable");
  const int count_row = row_by_id(chunks, "chunks.count");
  QVERIFY(count_row >= 0);
  QVERIFY(chunks->rowAt(count_row)->value.has_value());
  QVERIFY(*chunks->rowAt(count_row)->value >= 3);
  QCOMPARE(chunks->data(chunks->index(count_row, StatisticsTableModel::Status),
                        Qt::DisplayRole)
               .toString(),
           QStringLiteral("ready"));
  QVERIFY(progress_label(window)->text().contains(
      QStringLiteral("Statistics ready")));
  QVERIFY(!progress_label(window)->text().contains(
      QStringLiteral("partial"), Qt::CaseInsensitive));

  // GUI export == shared serializer == real CLI stdout, JSON and CSV.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString json_path = dir.filePath(QStringLiteral("ready.json"));
  const QString csv_path = dir.filePath(QStringLiteral("ready.csv"));

  const auto reference = collect_reference(fixture_path(relative));
  const auto expected_json = pnga::statistics::serialize_statistics_json(
      reference.document, reference.snapshot);
  QVERIFY(expected_json.success);
  const auto expected_csv = pnga::statistics::serialize_statistics_csv(
      reference.document, reference.snapshot);
  QVERIFY(expected_csv.success);

  int json_exit = -1;
  QByteArray cli_json;
  run_cli(fixture_path(relative), "json", &json_exit, &cli_json);
  QCOMPARE(json_exit, 0);
  int csv_exit = -1;
  QByteArray cli_csv;
  run_cli(fixture_path(relative), "csv", &csv_exit, &cli_csv);
  QCOMPARE(csv_exit, 0);
  QVERIFY(!cli_json.isEmpty());
  QVERIFY(!cli_csv.isEmpty());

  auto* controller = window.findChild<StatisticsController*>();
  QVERIFY(controller != nullptr);
  controller->setSavePathCallback([json_path] { return json_path; });
  statistics_button(window, "statisticsExportJson")->click();
  QVERIFY(QFile::exists(json_path));
  QByteArray gui_json;
  read_file(json_path, &gui_json);
  QCOMPARE(gui_json,
           QByteArray(expected_json.bytes.data(),
                      static_cast<int>(expected_json.bytes.size())));
  QCOMPARE(gui_json, cli_json);
  assert_no_path_or_clock_artifacts(gui_json);

  controller->setSavePathCallback([csv_path] { return csv_path; });
  statistics_button(window, "statisticsExportCsv")->click();
  QVERIFY(QFile::exists(csv_path));
  QByteArray gui_csv;
  read_file(csv_path, &gui_csv);
  QCOMPARE(gui_csv,
           QByteArray(expected_csv.bytes.data(),
                      static_cast<int>(expected_csv.bytes.size())));
  QCOMPARE(gui_csv, cli_csv);
  assert_no_path_or_clock_artifacts(gui_csv);
  QVector<QStringList> records;
  require_importable_csv(csv_path, &records);
  QVERIFY(records.size() >= 2);

  // Every section is honest ready: status/complete/scope present, no
  // falsely complete section, no path or clock artifacts.
  QJsonObject sections;
  exported_sections(gui_json, &sections);
  bool saw_not_ready = false;
  assert_honest_sections(sections, &saw_not_ready);
  QVERIFY2(!saw_not_ready,
           "a ready fixture must not carry a non-ready section");
}

// Malformed product pass: usable verified prefix, explicit Partial labeling,
// honest non-ready sections and exports that still byte-match the CLI. The
// decoder-level truncation raises no structural validation issue, so the
// frozen exit mapping yields 4 (incomplete statistics), not 3.
void StatisticsProductGateTest::malformedPartialExportHonestSections() {
  const char* relative = "malformed/error-truncated-token.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  // A decoder-level truncated token produces no reference image; the parsed
  // chunk structure survives (compression product gate precedent), which is
  // the document-ready gate for the statistics run.
  QVERIFY(window.openFile(fixture_path(relative)));
  QCoreApplication::processEvents();
  auto* chunk_model = window.findChild<pnga::ui::qt::ChunkModel*>();
  QVERIFY(chunk_model != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(chunk_model->rowCount() >= 3, kTimeoutMs);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kTimeoutMs);

  // Partial output stays exportable and is labeled explicitly.
  QVERIFY(statistics_button(window, "statisticsExportJson")->isEnabled());
  QVERIFY(statistics_button(window, "statisticsExportCsv")->isEnabled());
  QVERIFY(progress_label(window)->text().contains(
      QStringLiteral("Partial"), Qt::CaseInsensitive));

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString json_path = dir.filePath(QStringLiteral("partial.json"));
  const QString csv_path = dir.filePath(QStringLiteral("partial.csv"));
  const auto reference = collect_reference(fixture_path(relative));
  const auto expected_json = pnga::statistics::serialize_statistics_json(
      reference.document, reference.snapshot);
  QVERIFY(expected_json.success);
  const auto expected_csv = pnga::statistics::serialize_statistics_csv(
      reference.document, reference.snapshot);
  QVERIFY(expected_csv.success);
  int json_exit = -1;
  QByteArray cli_json;
  run_cli(fixture_path(relative), "json", &json_exit, &cli_json);
  QCOMPARE(json_exit, 4);
  int csv_exit = -1;
  QByteArray cli_csv;
  run_cli(fixture_path(relative), "csv", &csv_exit, &cli_csv);
  QCOMPARE(csv_exit, 4);

  auto* controller = window.findChild<StatisticsController*>();
  QVERIFY(controller != nullptr);
  controller->setSavePathCallback([json_path] { return json_path; });
  statistics_button(window, "statisticsExportJson")->click();
  QVERIFY(QFile::exists(json_path));
  QByteArray gui_json;
  read_file(json_path, &gui_json);
  QCOMPARE(gui_json,
           QByteArray(expected_json.bytes.data(),
                      static_cast<int>(expected_json.bytes.size())));
  QCOMPARE(gui_json, cli_json);

  controller->setSavePathCallback([csv_path] { return csv_path; });
  statistics_button(window, "statisticsExportCsv")->click();
  QVERIFY(QFile::exists(csv_path));
  QByteArray gui_csv;
  read_file(csv_path, &gui_csv);
  QCOMPARE(gui_csv,
           QByteArray(expected_csv.bytes.data(),
                      static_cast<int>(expected_csv.bytes.size())));
  QCOMPARE(gui_csv, cli_csv);
  QVERIFY(progress_label(window)->text().contains(
      QStringLiteral("partial"), Qt::CaseInsensitive));

  // Honest sections: at least one section is not ready; the DEFLATE token
  // section never claims completeness.
  QJsonObject sections;
  exported_sections(gui_json, &sections);
  bool saw_not_ready = false;
  assert_honest_sections(sections, &saw_not_ready);
  QVERIFY2(saw_not_ready,
           "a malformed fixture must not report every section ready");
  const QJsonObject tokens =
      sections.value(QStringLiteral("tokens")).toObject();
  QVERIFY(tokens.value(QStringLiteral("status")).toString() !=
          QStringLiteral("ready"));

  QVector<QStringList> records;
  require_importable_csv(csv_path, &records);
  // The verified prefix serializes real collected values, including zeros;
  // the empty-versus-zero distinction is pinned by the partial-v1 CSV golden.
  bool saw_zero_value = false;
  for (const QStringList& record : records) {
    if (record.at(4) == QStringLiteral("0")) {
      saw_zero_value = true;
    }
  }
  QVERIFY(saw_zero_value);
}

// Large product pass: perf-large-rgba8 finishes whole-document collection,
// the token section stays honest ready and the scalar scan retains O(1)
// token records under the frozen 64 MiB working-memory reservation.
void StatisticsProductGateTest::largeCollectionBoundedRetention() {
  const char* relative = "valid/perf-large-rgba8.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, relative);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kLargeTimeoutMs);

  // The published view carries the verified token prefix. The frozen
  // WP-602A default sample budget (2^20) is smaller than the 2,359,296
  // stored literals of perf-large-rgba8, so the honest bounded outcome is
  // budget_exceeded with the collected verified prefix — never a falsely
  // complete section.
  auto* deflate_model = table_model(window, "statisticsDeflateTable");
  const int token_row = row_by_id(deflate_model, "tokens.count");
  QVERIFY(token_row >= 0);
  QVERIFY(deflate_model->rowAt(token_row)->value.has_value());
  QVERIFY(*deflate_model->rowAt(token_row)->value > 0);
  QCOMPARE(deflate_model->data(
               deflate_model->index(token_row, StatisticsTableModel::Status),
               Qt::DisplayRole)
               .toString(),
           QStringLiteral("budget_exceeded"));
  QVERIFY(progress_label(window)->text().contains(
      QStringLiteral("Partial"), Qt::CaseInsensitive));

  // Export byte equality on the large document too (GUI == serializer).
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString json_path = dir.filePath(QStringLiteral("large.json"));
  const auto reference = collect_reference(fixture_path(relative));
  const auto expected_json = pnga::statistics::serialize_statistics_json(
      reference.document, reference.snapshot);
  QVERIFY(expected_json.success);
  auto* controller = window.findChild<StatisticsController*>();
  QVERIFY(controller != nullptr);
  controller->setSavePathCallback([json_path] { return json_path; });
  statistics_button(window, "statisticsExportJson")->click();
  QVERIFY(QFile::exists(json_path));
  QByteArray exported;
  read_file(json_path, &exported);
  QCOMPARE(exported,
           QByteArray(expected_json.bytes.data(),
                      static_cast<int>(expected_json.bytes.size())));
  QJsonObject sections;
  exported_sections(exported, &sections);
  bool saw_not_ready = false;
  assert_honest_sections(sections, &saw_not_ready);
  QVERIFY2(saw_not_ready,
           "the bounded large collection must keep the token section "
           "honestly incomplete");
  const QJsonObject tokens =
      sections.value(QStringLiteral("tokens")).toObject();
  QCOMPARE(tokens.value(QStringLiteral("status")).toString(),
           QStringLiteral("budget_exceeded"));
  QCOMPARE(tokens.value(QStringLiteral("complete")).toBool(), false);
  QCOMPARE(tokens.value(QStringLiteral("count")).toInt(), 1 << 20);
  for (const char* fast_id : {"overview", "chunks", "filters", "blocks"}) {
    QCOMPARE(sections.value(QString::fromLatin1(fast_id))
                 .toObject()
                 .value(QStringLiteral("status"))
                 .toString(),
             QStringLiteral("ready"));
  }

  // O(1) retention on the same logical stream: the scalar scan holds at
  // most one in-flight token record and never an output/event list.
  QFile png(fixture_path(relative));
  QVERIFY(png.open(QIODevice::ReadOnly));
  const QByteArray png_bytes = png.readAll();
  png.close();
  const std::byte* raw_begin =
      reinterpret_cast<const std::byte*>(png_bytes.constData());
  const pnga::io::MemoryByteSource source(
      std::vector<std::byte>(raw_begin, raw_begin + png_bytes.size()));
  const pnga::png_format::ChunkIndex chunks =
      pnga::png_format::index_chunks(source);
  QVERIFY(chunks.valid_signature);
  pnga::png_format::VirtualIDATStream stream(chunks);
  VirtualIdatGateSource logical(stream, source);
  pnga::deflate_trace::TokenScanOptions options;
  options.observer = [](const pnga::deflate_trace::TokenFact&) {
    return true;
  };
  const pnga::deflate_trace::TokenScanResult scan =
      pnga::deflate_trace::scan_tokens(logical, options);
  QCOMPARE(scan.status, pnga::deflate_trace::TokenScanStatus::kReady);
  QVERIFY2(scan.peak_retained_token_records <= 1,
           "the scalar scan must retain at most one token record");
  QCOMPARE(scan.stream_ended, true);

  // The frozen declared working-memory reservation (ruling R6).
  QCOMPARE(pnga::analysis_engine::StatisticsCollectionRequest{}
               .max_working_bytes,
           kMaxWorkingBytes);
}

// Rapid switch and stale generation: a collection that is overtaken by two
// rapid document replacements publishes nothing stale; the final view and
// export belong to the current document only.
void StatisticsProductGateTest::rapidSwitchStaleGenerationNonPublication() {
  const char* first = "valid/ui-gray1-none.png";
  const char* second = "valid/trace-stored-literals.png";
  const char* third = "valid/trace-dynamic-overlap-repeats.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, first);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kTimeoutMs);
  QCOMPARE(finished.front().front().value<std::uint64_t>(),
           session->generation());

  // Refresh starts a new run and two rapid replacements overtake it; the
  // stale run publishes no finished result for the replaced document.
  statistics_button(window, "statisticsRefresh")->click();
  QVERIFY(window.openFile(fixture_path(second)));
  QVERIFY(window.openFile(fixture_path(third)));
  QCoreApplication::processEvents();
  QTest::qWait(100);
  QCOMPARE(finished.count(), 1);

  // Re-activation after the rapid switch requests the new document once.
  inspector_tabs(window)->setCurrentIndex(0);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, kLargeTimeoutMs);
  QCOMPARE(finished.back().front().value<std::uint64_t>(),
           session->generation());

  // The published view belongs to the current document: exported bytes
  // equal the direct serializer output for the third fixture only.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString json_path = dir.filePath(QStringLiteral("switched.json"));
  const auto reference = collect_reference(fixture_path(third));
  const auto expected_json = pnga::statistics::serialize_statistics_json(
      reference.document, reference.snapshot);
  QVERIFY(expected_json.success);
  auto* controller = window.findChild<StatisticsController*>();
  QVERIFY(controller != nullptr);
  controller->setSavePathCallback([json_path] { return json_path; });
  statistics_button(window, "statisticsExportJson")->click();
  QVERIFY(QFile::exists(json_path));
  QByteArray exported;
  read_file(json_path, &exported);
  QCOMPARE(exported,
           QByteArray(expected_json.bytes.data(),
                      static_cast<int>(expected_json.bytes.size())));
  QJsonObject switched_sections;
  exported_sections(exported, &switched_sections);
  QCOMPARE(switched_sections.value(QStringLiteral("chunks"))
               .toObject()
               .value(QStringLiteral("count"))
               .toInt(),
           static_cast<int>(reference.snapshot.chunks.data.count));

  // Stale occurrence result publishes nothing: request an occurrence for
  // the published current view and replace the document before it can
  // finish. The bus generation was already updated by the replacement.
  auto* bus = window.findChild<pnga::ui::qt::SelectionBus*>();
  QVERIFY(bus != nullptr);
  bus->setDocumentGeneration(session->generation());
  BusRecorder recorder(&window);
  QObject::connect(bus, &pnga::ui::qt::SelectionBus::selectionChanged,
                   &recorder, &BusRecorder::onSelectionChanged);
  auto* pages = statistics_inspector(window)
                    ->findChild<QTabWidget*>(QStringLiteral("statisticsPages"));
  pages->setCurrentIndex(1);
  auto* chunks_model = table_model(window, "statisticsChunksTable");
  const int idat_row = row_by_id(chunks_model, "chunks.IDAT");
  QVERIFY(idat_row >= 0);
  statistics_table(window, "statisticsChunksTable")
      ->selectionModel()
      ->select(chunks_model->index(idat_row, 0),
               QItemSelectionModel::ClearAndSelect |
                   QItemSelectionModel::Rows);
  statistics_button(window, "statisticsShowOccurrence")->click();
  QVERIFY(window.openFile(fixture_path(first)));
  QTest::qWait(300);
  // The stale statistics occurrence publishes nothing; the only bus traffic
  // is the unchanged document-open pipeline of the replacement itself.
  QCOMPARE(recorder.statistics_count, 0);

  // The lifecycle settles: no stale worker survives the replacement.
  auto* controller_worker_owner = window.findChild<StatisticsController*>();
  QVERIFY(controller_worker_owner != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      controller_worker_owner->findChildren<StatisticsOccurrenceWorker*>()
          .isEmpty(),
      kTimeoutMs);
}

// Cancel keeps every verified row: the tables are never cleared and the
// chunks row keeps its frozen outcome.
void StatisticsProductGateTest::cancelRetainsVerifiedRows() {
  const char* relative = "valid/ui-gray1-none.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, relative);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kTimeoutMs);
  auto* chunks = table_model(window, "statisticsChunksTable");
  const int count_row = row_by_id(chunks, "chunks.count");
  QVERIFY(count_row >= 0);
  QCOMPARE(chunks->rowAt(count_row)->value, std::optional<std::uint64_t>{3});

  statistics_button(window, "statisticsRefresh")->click();
  statistics_button(window, "statisticsCancel")->click();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, kTimeoutMs);
  QVERIFY(chunks->rowCount() >= 2);
  const int retained_row = row_by_id(chunks, "chunks.count");
  QVERIFY(retained_row >= 0);
  const QString status =
      chunks->data(chunks->index(retained_row, StatisticsTableModel::Status),
                   Qt::DisplayRole)
          .toString();
  QVERIFY(status == QStringLiteral("ready") ||
          status == QStringLiteral("cancelled"));
  const auto value = chunks->rowAt(retained_row)->value;
  QVERIFY(value.has_value());
  QVERIFY(*value == 3 || *value == 0);

  statistics_button(window, "statisticsRefresh")->click();
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 3, kTimeoutMs);
  const int restored_row = row_by_id(chunks, "chunks.count");
  QVERIFY(restored_row >= 0);
  QCOMPARE(chunks->rowAt(restored_row)->value,
           std::optional<std::uint64_t>{3});
}

// Three locale display passes: the display role follows the current QLocale
// while every export stays byte-identical (locale-independent bytes).
void StatisticsProductGateTest::localeDisplayPassesKeepExportBytes() {
  const char* relative = "valid/perf-large-rgba8.png";
  QLocale::setDefault(QLocale::c());
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, relative);
  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kLargeTimeoutMs);

  auto* deflate = table_model(window, "statisticsDeflateTable");
  const int token_row = row_by_id(deflate, "tokens.count");
  QVERIFY(token_row >= 0);
  const QModelIndex value_index =
      deflate->index(token_row, StatisticsTableModel::Value);
  const auto raw =
      deflate->data(value_index, pnga::ui::qt::RawValueRole).value<quint64>();
  QVERIFY(raw > 999);  // grouping only differs on values above 999

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString json_path = dir.filePath(QStringLiteral("locale.json"));
  const QString csv_path = dir.filePath(QStringLiteral("locale.csv"));
  auto* controller = window.findChild<StatisticsController*>();
  QVERIFY(controller != nullptr);
  controller->setSavePathCallback([json_path] { return json_path; });
  statistics_button(window, "statisticsExportJson")->click();
  controller->setSavePathCallback([csv_path] { return csv_path; });
  statistics_button(window, "statisticsExportCsv")->click();
  QByteArray json_baseline;
  read_file(json_path, &json_baseline);
  QByteArray csv_baseline;
  read_file(csv_path, &csv_baseline);
  QVERIFY(!json_baseline.isEmpty());
  QVERIFY(!csv_baseline.isEmpty());
  const QString c_display =
      deflate->data(value_index, Qt::DisplayRole).toString();
  QCOMPARE(c_display, QString::number(raw));

  struct LocalePass {
    const char* name;
    QLocale locale;
  };
  const LocalePass passes[] = {
      {"de_DE", QLocale(QLocale::German, QLocale::Germany)},
      {"ru_RU", QLocale(QLocale::Russian, QLocale::Russia)},
      {"zh_CN", QLocale(QLocale::Chinese, QLocale::China)},
  };
  for (const LocalePass& pass : passes) {
    QLocale::setDefault(pass.locale);
    QCoreApplication::processEvents();
    // The display role follows the current QLocale; the raw role stays raw.
    const QString localized =
        deflate->data(value_index, Qt::DisplayRole).toString();
    QVERIFY2(!localized.isEmpty(), pass.name);
    QCOMPARE(deflate->data(value_index, pnga::ui::qt::RawValueRole)
                 .value<quint64>(),
             raw);
    if (pass.locale.name() == QStringLiteral("de_DE")) {
      QVERIFY2(localized != c_display,
               "de_DE display must group digits differently from C");
    }
    // Exports remain byte-identical to the C-locale baseline.
    controller->setSavePathCallback([json_path] { return json_path; });
    statistics_button(window, "statisticsExportJson")->click();
    QByteArray json_bytes;
    read_file(json_path, &json_bytes);
    QCOMPARE(json_bytes, json_baseline);
    controller->setSavePathCallback([csv_path] { return csv_path; });
    statistics_button(window, "statisticsExportCsv")->click();
    QByteArray csv_bytes;
    read_file(csv_path, &csv_bytes);
    QCOMPARE(csv_bytes, csv_baseline);
  }
  QLocale::setDefault(QLocale::c());
  QCoreApplication::processEvents();
}

// 320 px Inspector, keyboard chain, occurrence navigation and accessible
// names/roles on the real MainWindow.
void StatisticsProductGateTest::inspector320KeyboardAccessibilityOccurrence() {
  const char* relative = "valid/ui-gray1-none.png";
  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto* session = session_of(window);
  QVERIFY(session != nullptr);
  QSignalSpy finished(session, &DocumentSession::statisticsFinished);
  QVERIFY(finished.isValid());

  open_and_wait_ready(window, relative);
  auto* dock = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(dock != nullptr);
  auto* inspector = statistics_inspector(window);
  QVERIFY(inspector != nullptr);
  inspector_tabs(window)->setCurrentIndex(2);
  inspector->setFixedWidth(kNarrowWidth);
  window.resizeDocks({dock}, {kNarrowWidth + 24}, Qt::Horizontal);
  window.resize(kWindowWidth, kWindowHeight);
  QCoreApplication::processEvents();
  QCOMPARE(inspector->width(), kNarrowWidth);
  QVERIFY2(inspector->minimumWidth() <= kNarrowWidth,
           "the statistics inspector must not grow beyond 320 px");
  for (const char* name :
       {"statisticsOverviewTable", "statisticsChunksTable",
        "statisticsFiltersTable", "statisticsDeflateTable"}) {
    auto* table = statistics_table(window, name);
    QVERIFY2(table->width() > 0 && table->width() <= kNarrowWidth, name);
    QCOMPARE(table->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(table->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
  }

  select_statistics_tab(window);
  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kTimeoutMs);

  // Accessible names/roles: tables are Tables, actions are PushButtons and
  // every model index carries accessible text beyond color.
  for (const char* name :
       {"statisticsOverviewTable", "statisticsChunksTable",
        "statisticsFiltersTable", "statisticsDeflateTable"}) {
    auto* table = statistics_table(window, name);
    const auto* table_iface = QAccessible::queryAccessibleInterface(table);
    QVERIFY(table_iface != nullptr);
    QCOMPARE(table_iface->role(), QAccessible::Table);
    QVERIFY(!table_iface->text(QAccessible::Name).isEmpty());
  }
  for (const char* name :
       {"statisticsRefresh", "statisticsCancel", "statisticsExportJson",
        "statisticsExportCsv", "statisticsShowOccurrence"}) {
    auto* button = statistics_button(window, name);
    const auto* button_iface = QAccessible::queryAccessibleInterface(button);
    QVERIFY(button_iface != nullptr);
    QCOMPARE(button_iface->role(), QAccessible::PushButton);
    QVERIFY(!button_iface->text(QAccessible::Name).isEmpty());
  }
  auto* chunks = table_model(window, "statisticsChunksTable");
  const int count_row = row_by_id(chunks, "chunks.count");
  QVERIFY(count_row >= 0);
  QVERIFY(!chunks->data(chunks->index(count_row, 0), Qt::AccessibleTextRole)
               .toString()
               .isEmpty());
  QVERIFY(chunks->data(chunks->index(count_row, 0),
                       Qt::AccessibleDescriptionRole)
              .toString()
              .contains(QStringLiteral("section status")));
  QVERIFY(!QAccessible::queryAccessibleInterface(progress_label(window))
               ->text(QAccessible::Name)
               .isEmpty());

  // Keyboard tab chains cover every action and every page table: for each
  // current page, Refresh walks Refresh→Cancel→Export JSON→Export CSV→
  // [Show occurrence]→the visible page table. Show occurrence participates
  // only when the current page has a selected navigation row (it is disabled
  // and skipped otherwise, mirroring the widget-level contract).
  auto* pages = statistics_inspector(window)
                    ->findChild<QTabWidget*>(QStringLiteral("statisticsPages"));
  const char* page_table_names[4] = {
      "statisticsOverviewTable", "statisticsChunksTable",
      "statisticsFiltersTable", "statisticsDeflateTable"};
  for (int page = 0; page < 4; ++page) {
    pages->setCurrentIndex(page);
    const bool has_navigation = select_first_navigation_row(
        table_model(window, page_table_names[page]),
        statistics_table(window, page_table_names[page]));
    statistics_button(window, "statisticsRefresh")->setFocus();
    QVERIFY(statistics_button(window, "statisticsRefresh")->hasFocus());
    QStringList expected{QStringLiteral("statisticsCancel"),
                         QStringLiteral("statisticsExportJson"),
                         QStringLiteral("statisticsExportCsv")};
    if (has_navigation) {
      expected << QStringLiteral("statisticsShowOccurrence");
    }
    expected << QString::fromLatin1(page_table_names[page]);
    for (const QString& expected_name : expected) {
      QTest::keyClick(&window, Qt::Key_Tab);
      QWidget* reached = window.focusWidget();
      const QString reached_name =
          reached == nullptr ? QString() : reached->objectName();
      QVERIFY2(reached_name == expected_name,
               qPrintable(QStringLiteral("page %1: expected %2 but reached %3")
                              .arg(page)
                              .arg(expected_name, reached_name)));
    }
  }

  // Enter on a navigation row requests the typed occurrence and publishes
  // exactly once through the existing SelectionBus.
  auto* bus = window.findChild<pnga::ui::qt::SelectionBus*>();
  QVERIFY(bus != nullptr);
  bus->setDocumentGeneration(session->generation());
  BusRecorder recorder(&window);
  QObject::connect(bus, &pnga::ui::qt::SelectionBus::selectionChanged,
                   &recorder, &BusRecorder::onSelectionChanged);
  pages->setCurrentIndex(1);
  const int idat_row = row_by_id(chunks, "chunks.IDAT");
  QVERIFY(idat_row >= 0);
  auto* chunks_table = statistics_table(window, "statisticsChunksTable");
  chunks_table->selectionModel()->select(
      chunks->index(idat_row, 0),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  chunks_table->setFocus();
  QTest::keyClick(chunks_table, Qt::Key_Return);
  QTRY_VERIFY_WITH_TIMEOUT(recorder.count == 1, kTimeoutMs);
  QCOMPARE(recorder.last_origin, 4);
  QCOMPARE(recorder.last.stage, pnga::trace_model::Stage::kChunk);
  QVERIFY(!recorder.last.physical_spans.empty());

  // Escape anywhere cancels the running collection.
  QSignalSpy cancelled(inspector, &StatisticsInspector::cancelRequested);
  QVERIFY(cancelled.isValid());
  statistics_button(window, "statisticsRefresh")->click();
  QTest::keyClick(inspector, Qt::Key_Escape);
  QTRY_VERIFY_WITH_TIMEOUT(cancelled.count() >= 1, kTimeoutMs);
  QTest::qWait(100);
}

QTEST_MAIN(StatisticsProductGateTest)
#include "statistics_product_gate_test.moc"
