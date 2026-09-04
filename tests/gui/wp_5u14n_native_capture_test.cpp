// WP-5U14N native theme evidence capture target (R1: test-only). Renders the
// real MainWindow under the real platform theme engine — never offscreen —
// walks the frozen views for one ApplicationTheme::ThemeMode per CTest entry
// and writes one QWidget::grab() PNG per view plus one
// pnga-wp5u14n-native-capture-v1 record per matrix cell (R2) under
// PNGA_WP5U14N_OUT. Under QT_QPA_PLATFORM=offscreen every entry writes
// {"result":"skipped-offscreen"} records and exits 0 so the regular dev suite
// stays deterministic and green (R7). The record contract is self-asserted in
// initTestCase before any capture runs.

#include "main_window.h"

#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/delivered_image_view.h>

#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QTabWidget>
#include <QTemporaryDir>

#include <QStringList>
#include <QVector>

#ifndef PNGA_WP607C_CORPUS_DIR
#error "PNGA_WP607C_CORPUS_DIR must be defined by the build"
#endif

namespace wp5u14n_capture {

constexpr int kTimeoutMs = 10000;
constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 760;
constexpr char kSchema[] = "pnga-wp5u14n-native-capture-v1";
constexpr char kResultCaptured[] = "captured";
constexpr char kResultSkippedOffscreen[] = "skipped-offscreen";

// Frozen view order (plan §Task 1): default page, Blocks, Huffman, Decode
// Trace, narrow Inspector, focus state, Stored view.
const QStringList& frozenViews() {
  static const QStringList views{
      QStringLiteral("default"),       QStringLiteral("blocks"),
      QStringLiteral("huffman"),       QStringLiteral("decode-trace"),
      QStringLiteral("narrow-inspector"), QStringLiteral("focus"),
      QStringLiteral("stored")};
  return views;
}

// R2 fixture pinning: WP-607C stable ids and their paths under the corpus dir.
struct FixtureRef {
  const char* id;
  const char* relative;
};

const QVector<FixtureRef>& frozenFixtures() {
  static const QVector<FixtureRef> fixtures{
      {"ui-rgb8-five-filters", "valid/ui-rgb8-five-filters.png"},
      {"ui-gray1-none", "valid/ui-gray1-none.png"},
      {"trace-stored-literals", "valid/trace-stored-literals.png"}};
  return fixtures;
}

QString fixtureRelativePath(const QString& fixture_id) {
  for (const FixtureRef& fixture : frozenFixtures()) {
    if (QString::fromLatin1(fixture.id) == fixture_id) {
      return QString::fromLatin1(fixture.relative);
    }
  }
  return QString();
}

QString fixtureIdForView(const QString& view) {
  if (view == QStringLiteral("narrow-inspector")) {
    return QStringLiteral("ui-gray1-none");
  }
  if (view == QStringLiteral("stored")) {
    return QStringLiteral("trace-stored-literals");
  }
  return QStringLiteral("ui-rgb8-five-filters");
}

QString fixturePath(const QString& relative) {
  return QDir(QString::fromLatin1(PNGA_WP607C_CORPUS_DIR)).filePath(relative);
}

QString outputRoot() {
  const QString from_env = qEnvironmentVariable("PNGA_WP5U14N_OUT");
  return from_env.isEmpty() ? QStringLiteral("build/evidence/wp-5u14n")
                            : from_env;
}

QString gitCommit() {
  const QString from_env = qEnvironmentVariable("PNGA_WP5U14N_COMMIT");
  if (!from_env.isEmpty()) {
    return from_env;
  }
#ifdef PNGA_WP5U14N_GIT_COMMIT
  return QString::fromLatin1(PNGA_WP5U14N_GIT_COMMIT);
#else
  return QString();
#endif
}

bool isOffscreen() {
  return QGuiApplication::platformName() == QLatin1String("offscreen") ||
         qEnvironmentVariable("QT_QPA_PLATFORM") ==
             QLatin1String("offscreen");
}

// Host platform key for the frozen cell naming: macos or windows only.
QString platformKey() {
  const QString type = QSysInfo::productType();
  if (type == QStringLiteral("macos")) {
    return QStringLiteral("macos");
  }
  if (type == QStringLiteral("windows")) {
    return QStringLiteral("windows");
  }
  return type;
}

// mode_key is light | dark | system-light | system-dark; the frozen matrix
// names macOS cells *-retina and Windows 100% cells *-100.
QString cellId(const QString& platform, const QString& mode_key) {
  if (platform == QStringLiteral("macos")) {
    return QStringLiteral("mac-") + mode_key + QStringLiteral("-retina");
  }
  if (platform == QStringLiteral("windows")) {
    return QStringLiteral("win-") + mode_key + QStringLiteral("-100");
  }
  return QString();
}

QString modeKey(pnga::ui::qt::ApplicationTheme::ThemeMode requested,
                const QString& effective_mode) {
  const QString requested_name =
      pnga::ui::qt::ApplicationTheme::serializeMode(requested);
  if (requested_name == QStringLiteral("system")) {
    return QStringLiteral("system-") + effective_mode;
  }
  return requested_name;
}

struct CaptureEntry {
  QString view;
  QString fixture_id;
  QString fixture_sha256;
  QString capture_png;
  QString capture_png_sha256;
};

struct CaptureRecord {
  QString schema{QString::fromLatin1(kSchema)};
  QString cell;
  QString platform;
  QString os_build;
  QString architecture;
  QString qt_version;
  QString requested_mode;
  QString effective_mode;
  QString logical_dpi;
  QString device_pixel_ratio;
  QString window_size;
  QString git_commit;
  QString utc_timestamp;
  QString result{QString::fromLatin1(kResultCaptured)};
  QVector<CaptureEntry> captures;

  // Skip records stay minimal: only the fields this record actually carries
  // are serialized, so {"result":"skipped-offscreen"} cells stay valid (R7).
  QJsonObject to_json() const {
    QJsonObject object;
    const auto insert = [&object](const QString& name, const QString& value) {
      if (!value.isEmpty()) {
        object.insert(name, value);
      }
    };
    insert(QStringLiteral("schema"), schema);
    insert(QStringLiteral("cell"), cell);
    insert(QStringLiteral("platform"), platform);
    insert(QStringLiteral("os_build"), os_build);
    insert(QStringLiteral("architecture"), architecture);
    insert(QStringLiteral("qt_version"), qt_version);
    insert(QStringLiteral("requested_mode"), requested_mode);
    insert(QStringLiteral("effective_mode"), effective_mode);
    insert(QStringLiteral("logical_dpi"), logical_dpi);
    insert(QStringLiteral("device_pixel_ratio"), device_pixel_ratio);
    insert(QStringLiteral("window_size"), window_size);
    insert(QStringLiteral("git_commit"), git_commit);
    insert(QStringLiteral("utc_timestamp"), utc_timestamp);
    insert(QStringLiteral("result"), result);
    if (!captures.isEmpty()) {
      QJsonArray array;
      for (const CaptureEntry& entry : captures) {
        QJsonObject entry_object;
        entry_object.insert(QStringLiteral("view"), entry.view);
        entry_object.insert(QStringLiteral("fixture_id"), entry.fixture_id);
        entry_object.insert(QStringLiteral("fixture_sha256"),
                            entry.fixture_sha256);
        entry_object.insert(QStringLiteral("capture_png"), entry.capture_png);
        entry_object.insert(QStringLiteral("capture_png_sha256"),
                            entry.capture_png_sha256);
        array.append(entry_object);
      }
      object.insert(QStringLiteral("captures"), array);
    }
    return object;
  }
};

// Strict R2 validation for captured records: every required field present
// and non-empty, per-view fixture identity, PNG path shape and 64-hex hashes.
QStringList recordViolations(const CaptureRecord& record) {
  QStringList violations;
  const auto require = [&violations](const QString& value, const char* name) {
    if (value.trimmed().isEmpty()) {
      violations << QString::fromLatin1(name);
    }
  };
  if (record.schema != QString::fromLatin1(kSchema)) {
    violations << QStringLiteral("schema");
  }
  require(record.cell, "cell");
  require(record.platform, "platform");
  require(record.os_build, "os_build");
  require(record.architecture, "architecture");
  require(record.qt_version, "qt_version");
  require(record.requested_mode, "requested_mode");
  require(record.effective_mode, "effective_mode");
  require(record.logical_dpi, "logical_dpi");
  require(record.device_pixel_ratio, "device_pixel_ratio");
  require(record.window_size, "window_size");
  require(record.git_commit, "git_commit");
  require(record.utc_timestamp, "utc_timestamp");
  require(record.result, "result");
  if (record.platform != QStringLiteral("macos") &&
      record.platform != QStringLiteral("windows")) {
    violations << QStringLiteral("platform");
  }
  const QString mode_key =
      record.requested_mode == QStringLiteral("system")
          ? QStringLiteral("system-") + record.effective_mode
          : record.requested_mode;
  if (record.cell != cellId(record.platform, mode_key)) {
    violations << QStringLiteral("cell");
  }
  static const QRegularExpression kTimestamp(
      QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"));
  if (!kTimestamp.match(record.utc_timestamp).hasMatch()) {
    violations << QStringLiteral("utc_timestamp");
  }
  if (record.captures.size() != frozenViews().size()) {
    violations << QStringLiteral("captures");
  } else {
    for (int index = 0; index < record.captures.size(); ++index) {
      const CaptureEntry& entry = record.captures.at(index);
      const QString prefix =
          QStringLiteral("captures[%1].").arg(index);
      if (entry.view != frozenViews().at(index)) {
        violations << prefix + QStringLiteral("view");
      }
      if (entry.fixture_id != fixtureIdForView(entry.view)) {
        violations << prefix + QStringLiteral("fixture_id");
      }
      if (entry.fixture_sha256.size() != 64) {
        violations << prefix + QStringLiteral("fixture_sha256");
      }
      if (entry.capture_png_sha256.size() != 64) {
        violations << prefix + QStringLiteral("capture_png_sha256");
      }
      const QString expected_png = QStringLiteral("captures/") + record.cell +
                                   QStringLiteral("-") + entry.view +
                                   QStringLiteral(".png");
      if (entry.capture_png != expected_png) {
        violations << prefix + QStringLiteral("capture_png");
      }
    }
  }
  violations.removeDuplicates();
  return violations;
}

// R7: the offscreen skip path yields a minimal skip record for the cell the
// entry would have captured.
CaptureRecord makeSkipRecord(const QString& platform,
                             pnga::ui::qt::ApplicationTheme::ThemeMode requested,
                             const QString& os_mode) {
  CaptureRecord record;
  record.schema = QString::fromLatin1(kSchema);
  record.cell = cellId(platform, modeKey(requested, os_mode));
  record.result = QString::fromLatin1(kResultSkippedOffscreen);
  return record;
}

QString sha256OfFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
          .toHex());
}

QString fixtureSha256(const QString& fixture_id) {
  return sha256OfFile(fixturePath(fixtureRelativePath(fixture_id)));
}

// True when `path` holds a parsed record whose result is "captured" —
// native evidence an offscreen skip write must never destroy.
bool isCapturedRecord(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  return document.isObject() &&
         document.object().value(QStringLiteral("result")).toString() ==
             QStringLiteral("captured");
}

}  // namespace wp5u14n_capture

namespace {

constexpr int kStandardWidth = 480;

QString sha256OfFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
          .toHex());
}

// Per-view fixture assignment pinned by R2: the default page and the three
// Compression pages use the five-filter RGB case, the narrow Inspector uses
// the 1-bit gray case and the Stored view uses the stored-literals trace.
wp5u14n_capture::CaptureRecord syntheticRecord(const QString& platform,
                                               const QString& mode_key) {
  wp5u14n_capture::CaptureRecord record;
  record.cell = wp5u14n_capture::cellId(platform, mode_key);
  record.platform = platform;
  record.os_build = QStringLiteral("Synthetic OS 1.0 (build test)");
  record.architecture = QStringLiteral("test-arch");
  record.qt_version = QStringLiteral("6.test");
  if (mode_key.startsWith(QStringLiteral("system-"))) {
    record.requested_mode = QStringLiteral("system");
    record.effective_mode = mode_key.mid(QStringLiteral("system-").size());
  } else {
    record.requested_mode = mode_key;
    record.effective_mode = mode_key;
  }
  record.logical_dpi = QStringLiteral("96.00");
  record.device_pixel_ratio = QStringLiteral("2.00");
  record.window_size = QStringLiteral("1200x760");
  record.git_commit = QStringLiteral("synthetic-commit");
  record.utc_timestamp = QStringLiteral("2026-09-04T00:00:00Z");
  record.result = QString::fromLatin1(wp5u14n_capture::kResultCaptured);
  for (const QString& view : wp5u14n_capture::frozenViews()) {
    wp5u14n_capture::CaptureEntry entry;
    entry.view = view;
    entry.fixture_id = wp5u14n_capture::fixtureIdForView(view);
    entry.fixture_sha256 = QStringLiteral("0").repeated(64);
    entry.capture_png = QStringLiteral("captures/") + record.cell +
                        QStringLiteral("-") + view + QStringLiteral(".png");
    entry.capture_png_sha256 = QStringLiteral("0").repeated(64);
    record.captures.append(entry);
  }
  return record;
}

void openValidReady(MainWindow& window, const QString& fixture_id) {
  QVERIFY(window.openFile(wp5u14n_capture::fixturePath(
      wp5u14n_capture::fixtureRelativePath(fixture_id))));
  QCoreApplication::processEvents();
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(),
                           wp5u14n_capture::kTimeoutMs);
  auto* status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           wp5u14n_capture::kTimeoutMs);
}

void applyWidth(MainWindow& window, int width) {
  auto* dock = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(dock != nullptr);
  auto* container =
      window.findChild<QWidget*>(QStringLiteral("compressionContainer"));
  QVERIFY(container != nullptr);
  auto* groups =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(groups != nullptr);
  groups->setCurrentIndex(1);
  container->setFixedWidth(width);
  window.resizeDocks({dock}, {width + 24}, Qt::Horizontal);
  window.resize(wp5u14n_capture::kWindowWidth, wp5u14n_capture::kWindowHeight);
  QCoreApplication::processEvents();
  QCOMPARE(container->width(), width);
}

void captureView(QWidget* widget, const QString& out_root, const QString& cell,
                 const QString& view, const QString& fixture_id,
                 QVector<wp5u14n_capture::CaptureEntry>* entries) {
  const QString relative = QStringLiteral("captures/") + cell +
                           QStringLiteral("-") + view + QStringLiteral(".png");
  const QPixmap grabbed = widget->grab();
  QVERIFY(!grabbed.isNull());
  QVERIFY(grabbed.save(out_root + QStringLiteral("/") + relative, "PNG"));
  wp5u14n_capture::CaptureEntry entry;
  entry.view = view;
  entry.fixture_id = fixture_id;
  entry.fixture_sha256 = wp5u14n_capture::fixtureSha256(fixture_id);
  entry.capture_png = relative;
  entry.capture_png_sha256 =
      wp5u14n_capture::sha256OfFile(out_root + QStringLiteral("/") + relative);
  QVERIFY(!entry.fixture_sha256.isEmpty());
  QVERIFY(!entry.capture_png_sha256.isEmpty());
  entries->append(entry);
}

}  // namespace

class Wp5U14nNativeCaptureTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void initTestCase();
  void captureLight();
  void captureSystem();
  void captureDark();

 private:
  void runRequestedMode(pnga::ui::qt::ApplicationTheme::ThemeMode mode);
  void writeOffscreenSkipRecord(
      pnga::ui::qt::ApplicationTheme::ThemeMode mode);
};

void Wp5U14nNativeCaptureTest::init() {
  QSettings settings;
  settings.clear();
}

void Wp5U14nNativeCaptureTest::initTestCase() {
  // R2 contract: one record struct per frozen automated cell; every required
  // field present and non-empty, per-view fixture identity and PNG path in
  // the frozen shape, deterministic cell ids and exact view coverage.
  const QStringList platforms{QStringLiteral("macos"),
                              QStringLiteral("windows")};
  const QStringList mode_keys{QStringLiteral("light"), QStringLiteral("dark"),
                              QStringLiteral("system-light"),
                              QStringLiteral("system-dark")};
  for (const QString& platform : platforms) {
    for (const QString& mode_key : mode_keys) {
      const auto record = syntheticRecord(platform, mode_key);
      const auto violations = wp5u14n_capture::recordViolations(record);
      QVERIFY2(violations.isEmpty(),
               qPrintable(QStringLiteral("cell %1: %2")
                              .arg(record.cell, violations.join(';'))));
    }
  }

  auto stripped_field = syntheticRecord(QStringLiteral("macos"),
                                        QStringLiteral("light"));
  stripped_field.git_commit.clear();
  auto violations = wp5u14n_capture::recordViolations(stripped_field);
  QCOMPARE(violations.size(), 1);
  QVERIFY(violations.first().contains(QStringLiteral("git_commit")));

  auto stripped_entry = syntheticRecord(QStringLiteral("windows"),
                                        QStringLiteral("system-dark"));
  stripped_entry.captures[2].fixture_sha256.clear();
  violations = wp5u14n_capture::recordViolations(stripped_entry);
  QCOMPARE(violations.size(), 1);
  QVERIFY(violations.first().contains(QStringLiteral("fixture_sha256")));

  auto missing_view = syntheticRecord(QStringLiteral("macos"),
                                      QStringLiteral("dark"));
  missing_view.captures.removeLast();
  violations = wp5u14n_capture::recordViolations(missing_view);
  QVERIFY(!violations.isEmpty());

  auto wrong_fixture = syntheticRecord(QStringLiteral("macos"),
                                       QStringLiteral("system-light"));
  wrong_fixture.captures[1].fixture_id =
      QStringLiteral("ui-rgba16-byte-select");
  violations = wp5u14n_capture::recordViolations(wrong_fixture);
  QVERIFY(!violations.isEmpty());

  // Cell-id derivation matches the frozen matrix naming.
  QCOMPARE(wp5u14n_capture::cellId(QStringLiteral("macos"),
                                   QStringLiteral("light")),
           QStringLiteral("mac-light-retina"));
  QCOMPARE(wp5u14n_capture::cellId(QStringLiteral("macos"),
                                   QStringLiteral("system-dark")),
           QStringLiteral("mac-system-dark-retina"));
  QCOMPARE(wp5u14n_capture::cellId(QStringLiteral("windows"),
                                   QStringLiteral("system-light")),
           QStringLiteral("win-system-light-100"));
  QCOMPARE(wp5u14n_capture::cellId(QStringLiteral("windows"),
                                   QStringLiteral("dark")),
           QStringLiteral("win-dark-100"));

  // The fixture registry pins the WP-607C ids: the corpus files resolve,
  // hash to 64 hex and match the generated catalog whenever it is present.
  QHash<QString, QString> catalog_hashes;
  QFile catalog(wp5u14n_capture::fixturePath(QStringLiteral("index.json")));
  if (catalog.open(QIODevice::ReadOnly)) {
    const auto document = QJsonDocument::fromJson(catalog.readAll());
    QVERIFY(document.isObject());
    const auto cases =
        document.object().value(QStringLiteral("cases")).toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto& value : cases) {
      const auto entry = value.toObject();
      catalog_hashes.insert(entry.value(QStringLiteral("id")).toString(),
                            entry.value(QStringLiteral("expected_sha256"))
                                .toString());
    }
  }
  for (const auto& fixture : wp5u14n_capture::frozenFixtures()) {
    const QString id = QString::fromLatin1(fixture.id);
    const QString path = wp5u14n_capture::fixturePath(
        wp5u14n_capture::fixtureRelativePath(id));
    QVERIFY2(QFile::exists(path),
             qPrintable(QStringLiteral("missing corpus fixture %1").arg(id)));
    const QString sha = wp5u14n_capture::sha256OfFile(path);
    QVERIFY2(sha.size() == 64,
             qPrintable(QStringLiteral("fixture %1 sha256 malformed").arg(id)));
    const auto expected = catalog_hashes.constFind(id);
    if (expected != catalog_hashes.constEnd()) {
      QVERIFY2(expected.value() == sha,
               qPrintable(QStringLiteral("fixture %1 diverges from the corpus "
                                         "registry")
                              .arg(id)));
    }
  }

  // R7: the offscreen skip path yields skip records for the entry's cell.
  QCOMPARE(wp5u14n_capture::makeSkipRecord(QStringLiteral("macos"),
                                           pnga::ui::qt::ApplicationTheme::
                                               ThemeMode::kLight,
                                           QStringLiteral("light"))
               .result,
           QString::fromLatin1(wp5u14n_capture::kResultSkippedOffscreen));
  QCOMPARE(wp5u14n_capture::makeSkipRecord(QStringLiteral("windows"),
                                           pnga::ui::qt::ApplicationTheme::
                                               ThemeMode::kSystem,
                                           QStringLiteral("dark"))
               .cell,
           QStringLiteral("win-system-dark-100"));

  // Native-record preservation: only a parsed "captured" record blocks an
  // offscreen skip write; skip records and residue stay overwritable.
  QTemporaryDir scratch;
  QVERIFY(scratch.isValid());
  const QString captured_record = scratch.filePath(QStringLiteral("native.json"));
  {
    QJsonObject object;
    object.insert(QStringLiteral("result"), QStringLiteral("captured"));
    QFile output(captured_record);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    output.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  }
  QVERIFY(wp5u14n_capture::isCapturedRecord(captured_record));
  const QString skip_record = scratch.filePath(QStringLiteral("skip.json"));
  {
    QJsonObject object;
    object.insert(QStringLiteral("result"),
                  QString::fromLatin1(wp5u14n_capture::kResultSkippedOffscreen));
    QFile output(skip_record);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    output.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  }
  QVERIFY(!wp5u14n_capture::isCapturedRecord(skip_record));
  const QString garbage_record = scratch.filePath(QStringLiteral("garbage.json"));
  {
    QFile output(garbage_record);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    output.write("not json");
  }
  QVERIFY(!wp5u14n_capture::isCapturedRecord(garbage_record));
  QVERIFY(!wp5u14n_capture::isCapturedRecord(
      scratch.filePath(QStringLiteral("missing.json"))));
}

void Wp5U14nNativeCaptureTest::captureLight() {
  runRequestedMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight);
}

void Wp5U14nNativeCaptureTest::captureSystem() {
  runRequestedMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kSystem);
}

void Wp5U14nNativeCaptureTest::captureDark() {
  runRequestedMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
}

void Wp5U14nNativeCaptureTest::writeOffscreenSkipRecord(
    pnga::ui::qt::ApplicationTheme::ThemeMode mode) {
  const QString os_mode = qEnvironmentVariable("PNGA_WP5U14N_OS_MODE");
  const auto record = wp5u14n_capture::makeSkipRecord(
      wp5u14n_capture::platformKey(), mode,
      os_mode.isEmpty() ? QStringLiteral("light") : os_mode);
  const QString out_root = wp5u14n_capture::outputRoot();
  QVERIFY(QDir().mkpath(out_root + QStringLiteral("/records")));
  const QString record_path = out_root + QStringLiteral("/records/") +
                              record.cell + QStringLiteral(".json");
  // A routine offscreen dev-suite run must never clobber native evidence;
  // the runner's dirty-dir refusal stays the formal gate. Only a parsed
  // "captured" record blocks the skip write; overwriting another skip
  // record (or unparsable residue) remains allowed.
  if (wp5u14n_capture::isCapturedRecord(record_path)) {
    qInfo().noquote() << QStringLiteral("skip-preserved:") << record.cell;
    return;
  }
  QFile output(record_path);
  QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
  output.write(QJsonDocument(record.to_json()).toJson(QJsonDocument::Compact));
}

void Wp5U14nNativeCaptureTest::runRequestedMode(
    pnga::ui::qt::ApplicationTheme::ThemeMode mode) {
  if (wp5u14n_capture::isOffscreen()) {
    writeOffscreenSkipRecord(mode);
    QSKIP("QT_QPA_PLATFORM=offscreen: WP-5U14N capture is native-only (R7)");
  }
  const QString platform = wp5u14n_capture::platformKey();
  QVERIFY2(platform == QStringLiteral("macos") ||
               platform == QStringLiteral("windows"),
           qPrintable(QStringLiteral("unsupported native platform: %1")
                          .arg(platform)));
  const QString os_mode = qEnvironmentVariable("PNGA_WP5U14N_OS_MODE");
  QVERIFY2(os_mode.isEmpty() || os_mode == QStringLiteral("light") ||
               os_mode == QStringLiteral("dark"),
           "PNGA_WP5U14N_OS_MODE must be light or dark");

  pnga::ui::qt::ApplicationTheme theme(qApp);
  QVERIFY(theme.setMode(mode, /*persist=*/false));
  QCoreApplication::processEvents();
  const QString requested =
      pnga::ui::qt::ApplicationTheme::serializeMode(mode);
  const QString effective = pnga::ui::qt::ApplicationTheme::serializeMode(
      theme.effectiveMode());
  if (!os_mode.isEmpty()) {
    QVERIFY2(effective == os_mode,
             qPrintable(QStringLiteral("effective theme %1 does not match the "
                                       "OS appearance %2")
                            .arg(effective, os_mode)));
  }
  QVERIFY2(effective == QStringLiteral("light") ||
               effective == QStringLiteral("dark"),
           qPrintable(QStringLiteral("unresolved effective theme: %1")
                          .arg(effective)));

  const QString out_root = wp5u14n_capture::outputRoot();
  QVERIFY(QDir().mkpath(out_root + QStringLiteral("/captures")));
  QVERIFY(QDir().mkpath(out_root + QStringLiteral("/records")));
  const QString cell = wp5u14n_capture::cellId(
      platform, wp5u14n_capture::modeKey(mode, effective));

  MainWindow window(nullptr, &theme);
  window.resize(wp5u14n_capture::kWindowWidth, wp5u14n_capture::kWindowHeight);
  window.show();
  QCoreApplication::processEvents();

  QVector<wp5u14n_capture::CaptureEntry> entries;
  // 1. default page
  openValidReady(window, QStringLiteral("ui-rgb8-five-filters"));
  captureView(&window, out_root, cell, QStringLiteral("default"),
              QStringLiteral("ui-rgb8-five-filters"), &entries);

  // 2-4. Compression Blocks, Huffman, Decode Trace
  auto* groups =
      window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(groups != nullptr);
  auto* pages = window.findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
  QVERIFY(pages != nullptr);
  auto* container =
      window.findChild<QWidget*>(QStringLiteral("compressionContainer"));
  QVERIFY(container != nullptr);
  groups->setCurrentIndex(1);
  QCoreApplication::processEvents();
  pages->setCurrentIndex(0);
  QCoreApplication::processEvents();
  captureView(container, out_root, cell, QStringLiteral("blocks"),
              QStringLiteral("ui-rgb8-five-filters"), &entries);
  pages->setCurrentIndex(1);
  QCoreApplication::processEvents();
  captureView(container, out_root, cell, QStringLiteral("huffman"),
              QStringLiteral("ui-rgb8-five-filters"), &entries);
  pages->setCurrentIndex(2);
  QCoreApplication::processEvents();
  captureView(container, out_root, cell, QStringLiteral("decode-trace"),
              QStringLiteral("ui-rgb8-five-filters"), &entries);

  // 5. narrow Inspector: 360 logical width over ui-gray1-none
  openValidReady(window, QStringLiteral("ui-gray1-none"));
  applyWidth(window, 360);
  pages->setCurrentIndex(0);
  QCoreApplication::processEvents();
  captureView(&window, out_root, cell, QStringLiteral("narrow-inspector"),
              QStringLiteral("ui-gray1-none"), &entries);

  // 6. focus state: Tab onto one interactive control so the ring is visible
  openValidReady(window, QStringLiteral("ui-rgb8-five-filters"));
  applyWidth(window, kStandardWidth);
  pages->setCurrentIndex(0);
  QCoreApplication::processEvents();
  QWidget* focused = nullptr;
  for (int tabs = 0; tabs < 25 && focused == nullptr; ++tabs) {
    QTest::keyClick(&window, Qt::Key_Tab);
    QCoreApplication::processEvents();
    QWidget* candidate = window.focusWidget();
    if (candidate != nullptr && candidate != &window &&
        candidate->isEnabled() &&
        (candidate->focusPolicy() & Qt::TabFocus) != 0) {
      focused = candidate;
    }
  }
  QVERIFY2(focused != nullptr,
           "Tab focus did not land on an interactive control");
  captureView(&window, out_root, cell, QStringLiteral("focus"),
              QStringLiteral("ui-rgb8-five-filters"), &entries);

  // 7. Stored view: trace-stored-literals on the Huffman page
  openValidReady(window, QStringLiteral("trace-stored-literals"));
  applyWidth(window, kStandardWidth);
  groups->setCurrentIndex(1);
  pages->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* heading = pages->widget(1)->findChild<QLabel*>(
      QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains(QStringLiteral("Stored")),
                           wp5u14n_capture::kTimeoutMs);
  captureView(container, out_root, cell, QStringLiteral("stored"),
              QStringLiteral("trace-stored-literals"), &entries);

  wp5u14n_capture::CaptureRecord record;
  record.cell = cell;
  record.platform = platform;
  record.os_build = QSysInfo::prettyProductName();
  record.architecture = QSysInfo::currentCpuArchitecture();
  record.qt_version = QString::fromLatin1(qVersion());
  record.requested_mode = requested;
  record.effective_mode = effective;
  const auto* screen = QGuiApplication::primaryScreen();
  record.logical_dpi = QString::number(
      screen != nullptr ? screen->logicalDotsPerInch() : 0.0, 'f', 2);
  record.device_pixel_ratio =
      QString::number(window.devicePixelRatioF(), 'f', 2);
  record.window_size =
      QStringLiteral("%1x%2").arg(window.width()).arg(window.height());
  record.git_commit = wp5u14n_capture::gitCommit();
  record.utc_timestamp = QDateTime::currentDateTimeUtc().toString(
      QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
  record.captures = entries;

  const auto violations = wp5u14n_capture::recordViolations(record);
  QVERIFY2(violations.isEmpty(),
           qPrintable(QStringLiteral("record contract violations: %1")
                          .arg(violations.join(';'))));
  QFile output(out_root + QStringLiteral("/records/") + cell +
               QStringLiteral(".json"));
  QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
  output.write(QJsonDocument(record.to_json()).toJson(QJsonDocument::Compact));
}

QTEST_MAIN(Wp5U14nNativeCaptureTest)
#include "wp_5u14n_native_capture_test.moc"
