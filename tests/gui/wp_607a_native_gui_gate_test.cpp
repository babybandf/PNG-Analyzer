// WP-607A native GUI and accessibility gate (test-only, R8). Drives the real
// MainWindow through the frozen A01-A11 cells on a native Qt platform plugin
// and emits one pnga-wp607a-native-gui-v1 record (automated.json) under
// PNGA_WP607A_OUT. Under offscreen/minimal every behavioral slot skips and
// writeEvidence records no evidence, so the regular dev suite stays green and
// creates nothing (R9). Fixture identity is resolved through the WP-607C
// build-tree registry; records never embed absolute paths, usernames or
// hostnames (R7).

#include "main_window.h"

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_inspector_model.h>

#include <QtTest/QtTest>

#include <QAccessible>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeView>
#include <QUrl>

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#elif defined(Q_OS_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#ifndef PNGA_WP607C_CORPUS_DIR
#error "PNGA_WP607C_CORPUS_DIR must be defined by the build"
#endif

namespace wp607a {

constexpr int kReadyTimeoutMs = 10000;
constexpr int kTraceTimeoutMs = 10000;
constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 760;
constexpr char kSchema[] = "pnga-wp607a-native-gui-v1";
constexpr int kSchemaVersion = 1;
constexpr char kWorkPackage[] = "WP-607A";
constexpr char kCorpusRevision[] =
    "5df99ad82f145a3418a3c6715f76f677ca8194a02e86c0f810c6457aba92f16f";

const QStringList& automatedCells() {
  static const QStringList cells{
      QStringLiteral("A01"), QStringLiteral("A02"), QStringLiteral("A03"),
      QStringLiteral("A04"), QStringLiteral("A05"), QStringLiteral("A06"),
      QStringLiteral("A07"), QStringLiteral("A08"), QStringLiteral("A09"),
      QStringLiteral("A10"), QStringLiteral("A11")};
  return cells;
}

QString fixtureIdForCell(const QString& cell) {
  if (cell == QStringLiteral("A11")) {
    return QStringLiteral("trace-dynamic-overlap-repeats");
  }
  return QStringLiteral("ui-rgb8-five-filters");
}

// Readable role names for the A06 snapshot note; the default branch keeps
// unknown roles recordable without asserting them.
QString accessibleRoleText(QAccessible::Role role) {
  switch (role) {
    case QAccessible::MenuBar:
      return QStringLiteral("MenuBar");
    case QAccessible::PopupMenu:
      return QStringLiteral("PopupMenu");
    case QAccessible::MenuItem:
      return QStringLiteral("MenuItem");
    case QAccessible::Pane:
      return QStringLiteral("Pane");
    case QAccessible::Tree:
      return QStringLiteral("Tree");
    case QAccessible::Client:
      return QStringLiteral("Client");
    case QAccessible::PageTabList:
      return QStringLiteral("PageTabList");
    case QAccessible::Table:
      return QStringLiteral("Table");
    case QAccessible::SpinBox:
      return QStringLiteral("SpinBox");
    case QAccessible::CheckBox:
      return QStringLiteral("CheckBox");
    case QAccessible::Button:
      return QStringLiteral("Button");
    case QAccessible::StaticText:
      return QStringLiteral("StaticText");
    default:
      return QStringLiteral("Role(%1)").arg(static_cast<int>(role));
  }
}

// Re-execution ruling 3: role comparisons compare the role KIND (semantic
// name), not platform-specific raw role ids — the Windows accessibility
// bridge reports a QDockWidget as Window where Cocoa reports Pane, and both
// are the same pane/container kind. This strengthens the record (kind names
// in violations instead of raw ints) without changing the asserted
// semantics on any platform.
QString accessibleRoleKind(QAccessible::Role role) {
  switch (role) {
    case QAccessible::Pane:
    case QAccessible::Window:
      return QStringLiteral("Pane");
    default:
      break;
  }
  return accessibleRoleText(role);
}

QString expectedForCell(const QString& cell) {
  if (cell == QStringLiteral("A01")) {
    return QStringLiteral("Open fixture, visible title/state, close clears "
                          "document, reopen restores a usable document");
  }
  if (cell == QStringLiteral("A02")) {
    return QStringLiteral(".png/.PNG local URL accepted; non-PNG rejected; "
                          "opened document matches dropped fixture");
  }
  if (cell == QStringLiteral("A03")) {
    return QStringLiteral("File/View actions, native Open/Close/Quit "
                          "shortcuts and visibility toggles retain frozen "
                          "identities");
  }
  if (cell == QStringLiteral("A04")) {
    return QStringLiteral("both docks move/float and Reset Layout restores "
                          "areas, visibility and bounded widths");
  }
  if (cell == QStringLiteral("A05")) {
    return QStringLiteral("keyboard-only focus reaches coordinate, Preview, "
                          "Hex and Inspector controls without a trap");
  }
  if (cell == QStringLiteral("A06")) {
    return QStringLiteral("required controls expose non-empty stable names "
                          "and expected roles/states through QAccessible");
  }
  if (cell == QStringLiteral("A07")) {
    return QStringLiteral("selectable/copyable value round-trips through the "
                          "native clipboard without altering analysis state");
  }
  if (cell == QStringLiteral("A08")) {
    return QStringLiteral("12 alternating valid/malformed opens publish only "
                          "the final generation; close/reopen remains "
                          "responsive");
  }
  if (cell == QStringLiteral("A09")) {
    return QStringLiteral("selecting a Chunk navigates File Hex to its exact "
                          "source range");
  }
  if (cell == QStringLiteral("A10")) {
    return QStringLiteral("selecting a reconstruction stage/pixel updates "
                          "Current context without corrupting manual "
                          "selection");
  }
  return QStringLiteral("locked pixel resolves to bounded Trace and typed "
                        "compressed physical spans across the pipeline");
}

struct CellResult {
  QString id;
  QString fixture_id;
  QString expected;
  QString result;
  QString note;
};

struct FixtureFile {
  QString id;
  QString relative;
  QString sha256;
};

// Records one executed cell outcome per slot. The destructor captures slots
// that aborted on a failed assertion (FAIL, R9: an executed defect is never
// BLOCKED) or ended without completing (BLOCKED), so writeEvidence can later
// refuse incomplete records instead of silently dropping cells.
class CellScope {
 public:
  CellScope(QVector<CellResult>* cells, const char* id)
      : cells_(cells),
        id_(QString::fromLatin1(id)),
        fixture_id_(fixtureIdForCell(id_)),
        expected_(expectedForCell(id_)) {}

  ~CellScope() {
    if (completed_) {
      return;
    }
    const bool failed = QTest::currentTestFailed();
    cells_->append(CellResult{
        id_, fixture_id_, expected_,
        failed ? QStringLiteral("FAIL") : QStringLiteral("BLOCKED"),
        failed ? QStringLiteral("cell aborted on a failed assertion")
               : QStringLiteral("cell did not complete")});
  }

  CellScope(const CellScope&) = delete;
  CellScope& operator=(const CellScope&) = delete;

  void pass(const QString& note) {
    completed_ = true;
    cells_->append(
        CellResult{id_, fixture_id_, expected_, QStringLiteral("PASS"), note});
  }

  void fail(const QString& note) {
    completed_ = true;
    cells_->append(
        CellResult{id_, fixture_id_, expected_, QStringLiteral("FAIL"), note});
  }

 private:
  QVector<CellResult>* cells_;
  QString id_;
  QString fixture_id_;
  QString expected_;
  bool completed_ = false;
};

QString sha256OfFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
          .toHex());
}

bool nonNativePlatform() {
  const QString plugin = QGuiApplication::platformName();
  return plugin == QLatin1String("offscreen") ||
         plugin == QLatin1String("minimal");
}

QString platformId() {
  const QString from_env = qEnvironmentVariable("PNGA_WP607A_PLATFORM");
  if (!from_env.isEmpty()) {
    return from_env;
  }
  const QString type = QSysInfo::productType();
  if (type == QLatin1String("macos")) {
    return QStringLiteral("macos-arm64");
  }
  if (type == QLatin1String("windows")) {
    return QStringLiteral("windows-x64");
  }
  if (type == QLatin1String("linux") &&
      QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64")) {
    return QStringLiteral("ubuntu-lts-x64");
  }
  return type;
}

QString gitCommit() {
  const QString from_env = qEnvironmentVariable("PNGA_WP607A_COMMIT");
  if (!from_env.isEmpty()) {
    return from_env;
  }
#ifdef PNGA_WP607A_GIT_COMMIT
  return QString::fromLatin1(PNGA_WP607A_GIT_COMMIT);
#else
  return QString();
#endif
}

QString commandLine() {
  const QString from_env = qEnvironmentVariable("PNGA_WP607A_COMMAND");
  if (!from_env.isEmpty()) {
    return from_env;
  }
  return QStringLiteral("pnga_gui_wp_607a_native_gui_gate_tests");
}

QString compilerName() {
#if defined(Q_CC_MSVC)
  return QStringLiteral("msvc %1").arg(_MSC_VER);
#elif defined(Q_CC_CLANG)
  return QStringLiteral("clang %1.%2")
      .arg(__clang_major__)
      .arg(__clang_minor__);
#elif defined(Q_CC_GNU)
  return QStringLiteral("gcc %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__);
#else
  return QStringLiteral("unknown-compiler");
#endif
}

QString displaySession() {
  const QString plugin = QGuiApplication::platformName();
  if (plugin == QLatin1String("cocoa")) {
    return QStringLiteral("aqua");
  }
  if (plugin == QLatin1String("windows")) {
    return QStringLiteral("win32-desktop");
  }
  if (plugin == QLatin1String("xcb")) {
    const QString session = qEnvironmentVariable("XDG_SESSION_TYPE");
    return session.isEmpty() ? QStringLiteral("x11") : session;
  }
  return plugin;
}

QString totalMemoryDescription() {
  qint64 bytes = 0;
#if defined(Q_OS_MACOS)
  std::int64_t memsize = 0;
  std::size_t size = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) == 0) {
    bytes = static_cast<qint64>(memsize);
  }
#elif defined(Q_OS_WINDOWS)
  MEMORYSTATUSEX status;
  ZeroMemory(&status, sizeof(status));
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status) != 0) {
    bytes = static_cast<qint64>(status.ullTotalPhys);
  }
#else
  const qint64 pages = static_cast<qint64>(sysconf(_SC_PHYS_PAGES));
  const qint64 page_size = static_cast<qint64>(sysconf(_SC_PAGESIZE));
  // Checked arithmetic: guard the product against overflow before multiplying.
  if (pages > 0 && page_size > 0 &&
      pages <= (std::numeric_limits<qint64>::max() / page_size)) {
    bytes = pages * page_size;
  }
#endif
  if (bytes <= 0) {
    return QStringLiteral("unknown");
  }
  return QStringLiteral("%1 GiB").arg(bytes >> 30);
}

QString corpusDir() {
  return QString::fromLatin1(PNGA_WP607C_CORPUS_DIR);
}

QString fixturePath(const FixtureFile& fixture) {
  return QDir(corpusDir()).filePath(fixture.relative);
}

// Resolves each frozen id through the WP-607C build-tree registry (R6):
// refuses missing ids, SHA mismatches and source-tree corpus paths.
QVector<FixtureFile> resolveFixtures(QStringList* problems) {
  const QStringList kFrozenIds{
      QStringLiteral("ui-rgb8-five-filters"),
      QStringLiteral("trace-dynamic-overlap-repeats"),
      QStringLiteral("ui-gray1-none"),
      QStringLiteral("ui-rgba16-byte-select"),
      QStringLiteral("error-truncated-token")};
  const auto refuse = [problems](const QString& message) {
    if (problems != nullptr) {
      problems->append(message);
    }
  };
  QVector<FixtureFile> resolved;
  const QDir corpus(corpusDir());
  if (!corpus.exists()) {
    refuse(QStringLiteral("missing corpus directory"));
    return resolved;
  }
  // The generated registry lives under a build tree; a source-tree corpus
  // directory would bypass the WP-607C generator provenance.
  const QStringList parts = QDir::toNativeSeparators(corpus.absolutePath())
                                .split(QDir::separator(), Qt::SkipEmptyParts);
  if (!parts.contains(QLatin1String("build"))) {
    refuse(QStringLiteral("corpus directory looks like a source-tree path"));
    return resolved;
  }
  QFile index_file(corpus.filePath(QStringLiteral("index.json")));
  if (!index_file.open(QIODevice::ReadOnly)) {
    refuse(QStringLiteral("missing corpus index.json"));
    return resolved;
  }
  const auto document = QJsonDocument::fromJson(index_file.readAll());
  if (!document.isObject()) {
    refuse(QStringLiteral("corpus index.json is not a JSON object"));
    return resolved;
  }
  const auto index = document.object();
  if (index.value(QStringLiteral("corpus_revision")).toString() !=
      QString::fromLatin1(kCorpusRevision)) {
    refuse(QStringLiteral("corpus revision mismatch"));
    return resolved;
  }
  QHash<QString, QPair<QString, QString>> registry;
  const auto cases = index.value(QStringLiteral("cases")).toArray();
  for (const auto& value : cases) {
    const auto entry = value.toObject();
    registry.insert(entry.value(QStringLiteral("id")).toString(),
                    {entry.value(QStringLiteral("output")).toString(),
                     entry.value(QStringLiteral("expected_sha256")).toString()});
  }
  for (const QString& id : kFrozenIds) {
    if (!registry.contains(id)) {
      refuse(QStringLiteral("missing fixture id %1").arg(id));
      continue;
    }
    const QString output = registry.value(id).first;
    const QString expected = registry.value(id).second;
    // The generated output path must stay inside the corpus directory.
    if (output.isEmpty() || output.startsWith(QLatin1Char('/')) ||
        output.contains(QStringLiteral("\\")) ||
        output.split(QLatin1Char('/')).contains(QLatin1String(".."))) {
      refuse(QStringLiteral("unsafe fixture output for %1").arg(id));
      continue;
    }
    FixtureFile fixture;
    fixture.id = id;
    fixture.relative = output;
    const QString actual = sha256OfFile(fixturePath(fixture));
    if (actual.isEmpty()) {
      refuse(QStringLiteral("missing fixture file for %1").arg(id));
      continue;
    }
    if (actual != expected) {
      refuse(QStringLiteral("sha256 mismatch for %1").arg(id));
      continue;
    }
    fixture.sha256 = actual;
    resolved.append(fixture);
  }
  return resolved;
}

QJsonObject buildRecord(const QVector<FixtureFile>& fixtures,
                        const QVector<CellResult>& cells) {
  QJsonObject record;
  record.insert(QStringLiteral("schema"), QString::fromLatin1(kSchema));
  record.insert(QStringLiteral("schema_version"), kSchemaVersion);
  record.insert(QStringLiteral("work_package"),
                QString::fromLatin1(kWorkPackage));
  record.insert(QStringLiteral("platform"), platformId());
  record.insert(QStringLiteral("qt_platform_plugin"),
                QGuiApplication::platformName());
  record.insert(QStringLiteral("git_commit"), gitCommit());
  record.insert(QStringLiteral("command"), commandLine());
  record.insert(QStringLiteral("os_build"), QSysInfo::prettyProductName());
  record.insert(QStringLiteral("architecture"),
                QSysInfo::buildCpuArchitecture());
  record.insert(QStringLiteral("cpu"), QSysInfo::currentCpuArchitecture());
  record.insert(QStringLiteral("memory"), totalMemoryDescription());
  record.insert(QStringLiteral("machine_label"),
                QStringLiteral("wp607a-local-desktop"));
  record.insert(QStringLiteral("compiler"), compilerName());
  record.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
  record.insert(QStringLiteral("display_session"), displaySession());
  const auto* screen = QGuiApplication::primaryScreen();
  record.insert(QStringLiteral("logical_dpi"),
                QString::number(
                    screen != nullptr ? screen->logicalDotsPerInch() : 0.0,
                    'f', 2));
  record.insert(QStringLiteral("device_pixel_ratio"),
                QString::number(
                    screen != nullptr ? screen->devicePixelRatio() : 0.0, 'f',
                    2));
  record.insert(QStringLiteral("corpus_revision"),
                QString::fromLatin1(kCorpusRevision));
  QJsonObject fixture_hashes;
  for (const auto& fixture : fixtures) {
    fixture_hashes.insert(fixture.id, fixture.sha256);
  }
  record.insert(QStringLiteral("fixtures"), fixture_hashes);
  record.insert(QStringLiteral("utc_timestamp"),
                QDateTime::currentDateTimeUtc().toString(
                    QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")));
  record.insert(QStringLiteral("out_of_scope"),
                QJsonArray{QStringLiteral("statistics-export"),
                           QStringLiteral("apng-timeline")});
  QJsonArray cell_array;
  int fail_count = 0;
  int blocked_count = 0;
  for (const auto& cell : cells) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), cell.id);
    entry.insert(QStringLiteral("form"), QStringLiteral("automated"));
    entry.insert(QStringLiteral("fixture_id"), cell.fixture_id);
    entry.insert(QStringLiteral("expected"), cell.expected);
    entry.insert(QStringLiteral("result"), cell.result);
    entry.insert(QStringLiteral("note"), cell.note);
    const auto fixture = std::find_if(fixtures.cbegin(), fixtures.cend(),
                                      [&cell](const FixtureFile& candidate) {
                                        return candidate.id == cell.fixture_id;
                                      });
    entry.insert(QStringLiteral("fixture_sha256"),
                 fixture != fixtures.cend() ? fixture->sha256 : QString());
    cell_array.append(entry);
    if (cell.result == QLatin1String("FAIL")) {
      ++fail_count;
    } else if (cell.result == QLatin1String("BLOCKED")) {
      ++blocked_count;
    }
  }
  record.insert(QStringLiteral("cells"), cell_array);
  record.insert(QStringLiteral("status"),
                fail_count > 0 ? QStringLiteral("FAIL")
                : blocked_count > 0 ? QStringLiteral("BLOCKED")
                                    : QStringLiteral("PASS"));
  return record;
}

}  // namespace wp607a

class Wp607aNativeGuiGateTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void initTestCase();
  void openCloseReopen();
  void dragDrop();
  void menuShortcuts();
  void dockFloatReset();
  void keyboardFocus();
  void accessibleTree();
  void clipboard();
  void rapidSwitch();
  void chunkToFileBytes();
  void stageToPixel();
  void pixelToTokenBits();
  void writeEvidence();

 private:
  std::unique_ptr<MainWindow> shownWindow();
  void openValidReady(MainWindow& window, const wp607a::FixtureFile& fixture);
  wp607a::FixtureFile fixtureById(const QString& id) const;
  static QStringList focusChainNames(QWidget* window, QWidget* focused);
  static void requireAccessible(const QString& label, QObject* object,
                                QAccessible::Role role, bool require_name,
                                QStringList* snapshot, QStringList* violations);
  static void requireMenuItems(const QString& label, QMenu* menu,
                               QStringList* snapshot, QStringList* violations);

  QVector<wp607a::FixtureFile> fixtures_;
  QVector<wp607a::CellResult> cells_;
};

void Wp607aNativeGuiGateTest::init() {
  QSettings settings;
  settings.clear();
}

void Wp607aNativeGuiGateTest::initTestCase() {
  QStringList problems;
  fixtures_ = wp607a::resolveFixtures(&problems);
  QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("; "))));
  QCOMPARE(fixtures_.size(), 5);
  for (const QString& cell : wp607a::automatedCells()) {
    QVERIFY2(wp607a::expectedForCell(cell).trimmed().size() > 8,
             qPrintable(QStringLiteral("expected observation missing for %1")
                            .arg(cell)));
  }
}

std::unique_ptr<MainWindow> Wp607aNativeGuiGateTest::shownWindow() {
  auto window = std::make_unique<MainWindow>();
  window->resize(wp607a::kWindowWidth, wp607a::kWindowHeight);
  window->show();
  QCoreApplication::processEvents();
  return window;
}

void Wp607aNativeGuiGateTest::openValidReady(
    MainWindow& window, const wp607a::FixtureFile& fixture) {
  QVERIFY(window.openFile(wp607a::fixturePath(fixture)));
  QCoreApplication::processEvents();
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(),
                           wp607a::kReadyTimeoutMs);
  auto* status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           wp607a::kReadyTimeoutMs);
}

wp607a::FixtureFile Wp607aNativeGuiGateTest::fixtureById(
    const QString& id) const {
  for (const auto& fixture : fixtures_) {
    if (fixture.id == id) {
      return fixture;
    }
  }
  QTest::qFail("frozen fixture missing from the resolved registry", __FILE__,
               __LINE__);
  return wp607a::FixtureFile{};
}

// Focus identities: the object names of the focused widget and its ancestors
// below the window. Tab lands on inner widgets (tab bars, viewports) whose
// owning controls carry the frozen identities.
QStringList Wp607aNativeGuiGateTest::focusChainNames(QWidget* window,
                                                     QWidget* focused) {
  QStringList names;
  for (QWidget* node = focused;
       node != nullptr && node != window; node = node->parentWidget()) {
    const QString name = node->objectName();
    if (!name.isEmpty()) {
      names.append(name);
    }
  }
  return names;
}

void Wp607aNativeGuiGateTest::requireAccessible(const QString& label,
                                                QObject* object,
                                                QAccessible::Role role,
                                                bool require_name,
                                                QStringList* snapshot,
                                                QStringList* violations) {
  if (object == nullptr) {
    violations->append(label + QStringLiteral(":missing-object"));
    return;
  }
  const auto* iface = QAccessible::queryAccessibleInterface(object);
  if (iface == nullptr) {
    violations->append(label + QStringLiteral(":no-interface"));
    return;
  }
  // Re-execution ruling 3: compare role KINDs (semantic names) rather than
  // raw platform role ids; violations carry the kind names, never ints.
  if (wp607a::accessibleRoleKind(iface->role()) !=
      wp607a::accessibleRoleKind(role)) {
    violations->append(label + QStringLiteral(":role-kind-%1-expected-%2")
                                          .arg(wp607a::accessibleRoleKind(
                                                   iface->role()),
                                               wp607a::accessibleRoleKind(
                                                   role)));
  }
  const QString name = iface->text(QAccessible::Name);
  if (require_name && name.trimmed().isEmpty()) {
    violations->append(label + QStringLiteral(":empty-name"));
  }
  // The actual state/value tokens go into the snapshot evidence. Only
  // presence/format is asserted (a missing value is legal for most roles
  // and value text may vary), so the record stays deterministic.
  const QAccessible::State state = iface->state();
  QStringList state_tokens;
  if (state.disabled) {
    state_tokens.append(QStringLiteral("disabled"));
    if (role != QAccessible::MenuItem) {
      violations->append(label + QStringLiteral(":disabled"));
    }
  }
  if (state.invisible) {
    state_tokens.append(QStringLiteral("invisible"));
    if (role != QAccessible::MenuItem) {
      violations->append(label + QStringLiteral(":invisible"));
    }
  }
  if (state.focusable) {
    state_tokens.append(QStringLiteral("focusable"));
  }
  if (state.focused) {
    state_tokens.append(QStringLiteral("focused"));
  }
  if (state.checkable) {
    state_tokens.append(QStringLiteral("checkable"));
  }
  if (state.checked) {
    state_tokens.append(QStringLiteral("checked"));
  }
  const QString value = iface->text(QAccessible::Value);
  if (role == QAccessible::SpinBox &&
      !QRegularExpression(QStringLiteral("^-?\\d+$")).match(value).hasMatch()) {
    violations->append(label + QStringLiteral(":value-format"));
  }
  snapshot->append(QStringLiteral("%1: role %2, name '%3', state %4, value '%5'")
                       .arg(label, wp607a::accessibleRoleText(iface->role()),
                            name,
                            state_tokens.isEmpty()
                                ? QStringLiteral("none")
                                : state_tokens.join(QStringLiteral("+")),
                            value));
}

void Wp607aNativeGuiGateTest::requireMenuItems(const QString& label,
                                               QMenu* menu,
                                               QStringList* snapshot,
                                               QStringList* violations) {
  if (menu == nullptr) {
    violations->append(label + QStringLiteral(":missing-menu"));
    return;
  }
  const auto* iface = QAccessible::queryAccessibleInterface(menu);
  if (iface == nullptr) {
    violations->append(label + QStringLiteral(":no-interface"));
    return;
  }
  int items = 0;
  for (int i = 0; i < iface->childCount(); ++i) {
    const auto* child = iface->child(i);
    if (child == nullptr || child->role() != QAccessible::MenuItem) {
      continue;
    }
    ++items;
    const QString name = child->text(QAccessible::Name);
    if (name.trimmed().isEmpty()) {
      violations->append(label + QStringLiteral(":item-%1-empty-name").arg(i));
    }
    const QAccessible::State state = child->state();
    QStringList state_tokens;
    if (state.disabled) {
      state_tokens.append(QStringLiteral("disabled"));
    }
    if (state.focusable) {
      state_tokens.append(QStringLiteral("focusable"));
    }
    snapshot->append(QStringLiteral("%1[%2]: role MenuItem, name '%3', state %4")
                         .arg(label, QString::number(i), name,
                              state_tokens.isEmpty()
                                  ? QStringLiteral("none")
                                  : state_tokens.join(QStringLiteral("+"))));
  }
  if (items == 0) {
    violations->append(label + QStringLiteral(":no-menu-items"));
  }
}

void Wp607aNativeGuiGateTest::openCloseReopen() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A01");
  const auto fixture = fixtureById(QStringLiteral("ui-rgb8-five-filters"));
  const QString file_name =
      QFileInfo(wp607a::fixturePath(fixture)).fileName();
  auto window = shownWindow();
  openValidReady(*window, fixture);
  QVERIFY(window->windowTitle().contains(file_name));
  auto* close_action =
      window->findChild<QAction*>(QStringLiteral("closeImageAction"));
  auto* image = window->findChild<pnga::ui::qt::DeliveredImageView*>();
  auto* tree = window->findChild<QTreeView*>();
  QVERIFY(close_action != nullptr);
  QVERIFY(image != nullptr);
  QVERIFY(tree != nullptr);
  QVERIFY(close_action->isEnabled());
  QVERIFY(tree->model()->rowCount() > 0);

  close_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(!close_action->isEnabled());
  QVERIFY(image->image().isNull());
  QCOMPARE(tree->model()->rowCount(), 0);
  QVERIFY(!window->windowTitle().contains(file_name));
  auto* status = window->findChild<QLabel*>(QStringLiteral("pixelStatus"));
  QVERIFY(status != nullptr);
  QCOMPARE(status->text(), QStringLiteral("No image"));

  openValidReady(*window, fixture);
  QVERIFY(window->windowTitle().contains(file_name));
  QVERIFY(close_action->isEnabled());
  QVERIFY(tree->model()->rowCount() > 0);
  QVERIFY(!image->image().isNull());
  cell.pass(QStringLiteral("open, close and reopen of %1 kept title, image, "
                           "chunk tree and Close action coherent")
                .arg(file_name));
}

void Wp607aNativeGuiGateTest::dragDrop() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A02");
  const auto fixture = fixtureById(QStringLiteral("ui-rgb8-five-filters"));
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  // Re-execution ruling 1: the staging names must be distinct so a copy
  // never lands on a case-variant of an existing name — case-insensitive
  // filesystems (APFS, NTFS) treat wp607a-drop.PNG and wp607a-drop.png as
  // the same file, which aborted the cell before any product interaction.
  // The rejection case (non-PNG URL) stays testable via the .txt file.
  const QString upper = directory.filePath(QStringLiteral("wp607a-drop-upper.PNG"));
  const QString lower = directory.filePath(QStringLiteral("wp607a-drop-lower.png"));
  const QString text = directory.filePath(QStringLiteral("wp607a-not-a-png.txt"));
  QVERIFY(QFile::copy(wp607a::fixturePath(fixture), upper));
  QVERIFY(QFile::copy(wp607a::fixturePath(fixture), lower));
  {
    QFile text_file(text);
    QVERIFY(text_file.open(QIODevice::WriteOnly));
    QVERIFY(text_file.write("not a PNG") > 0);
  }

  auto window = shownWindow();
  const auto drop = [&window](const QString& path, bool expect_accepted) {
    QMimeData mime_data;
    mime_data.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter_event(QPoint(12, 12), Qt::CopyAction, &mime_data,
                                Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window.get(), &enter_event);
    QCOMPARE(enter_event.isAccepted(), expect_accepted);
    if (!expect_accepted) {
      return;
    }
    QDropEvent drop_event(QPointF(12, 12), Qt::CopyAction, &mime_data,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window.get(), &drop_event);
    QVERIFY(drop_event.isAccepted());
    QCoreApplication::processEvents();
  };

  drop(upper, true);
  QVERIFY(window->windowTitle().contains(QFileInfo(upper).fileName()));
  auto* tree = window->findChild<QTreeView*>();
  QVERIFY(tree != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount() > 0,
                           wp607a::kReadyTimeoutMs);

  drop(lower, true);
  QVERIFY(window->windowTitle().contains(QFileInfo(lower).fileName()));
  QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount() > 0,
                           wp607a::kReadyTimeoutMs);

  drop(text, false);
  QVERIFY(window->windowTitle().contains(QFileInfo(lower).fileName()));
  cell.pass(QStringLiteral("local .PNG and .png drops opened the dropped "
                           "document and the .txt drop was rejected"));
}

void Wp607aNativeGuiGateTest::menuShortcuts() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A03");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));

  QMenu* file_menu = nullptr;
  QMenu* view_menu = nullptr;
  for (QAction* action : window->menuBar()->actions()) {
    if (action->menu() == nullptr) {
      continue;
    }
    if (action->menu()->title() == QStringLiteral("&File")) {
      file_menu = action->menu();
    } else if (action->menu()->title() == QStringLiteral("&View")) {
      view_menu = action->menu();
    }
  }
  QVERIFY(file_menu != nullptr);
  QVERIFY(view_menu != nullptr);

  const auto action_by_name = [](QMenu* menu, const QString& name) {
    for (QAction* action : menu->actions()) {
      if (action->objectName() == name) {
        return action;
      }
    }
    return static_cast<QAction*>(nullptr);
  };
  QAction* open = nullptr;
  for (QAction* action : file_menu->actions()) {
    if (action->text() == QStringLiteral("&Open...")) {
      open = action;
    }
  }
  QVERIFY(open != nullptr);
  QCOMPARE(open->shortcut(), QKeySequence::Open);
  auto* close_action =
      window->findChild<QAction*>(QStringLiteral("closeImageAction"));
  auto* exit_action =
      window->findChild<QAction*>(QStringLiteral("exitAction"));
  QVERIFY(close_action != nullptr);
  QVERIFY(exit_action != nullptr);
  QCOMPARE(close_action->shortcut(), QKeySequence::Close);
  QCOMPARE(exit_action->shortcut(), QKeySequence::Quit);

  QAction* chunk_action =
      action_by_name(view_menu, QStringLiteral("showChunkList"));
  QAction* hex_action =
      action_by_name(view_menu, QStringLiteral("showHexView"));
  QAction* inspector_action =
      action_by_name(view_menu, QStringLiteral("showInspector"));
  QVERIFY(chunk_action != nullptr);
  QVERIFY(hex_action != nullptr);
  QVERIFY(inspector_action != nullptr);
  auto* chunks = window->findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector =
      window->findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  auto* hex_panel =
      window->findChild<QWidget*>(QStringLiteral("hexPanel"));
  QVERIFY(chunks != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(hex_panel != nullptr);
  QVERIFY(chunks->isVisible());
  QVERIFY(inspector->isVisible());
  QVERIFY(hex_panel->isVisible());
  chunk_action->trigger();
  hex_action->trigger();
  inspector_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(!chunks->isVisible());
  QVERIFY(!hex_panel->isVisible());
  QVERIFY(!inspector->isVisible());
  chunk_action->trigger();
  hex_action->trigger();
  inspector_action->trigger();
  QCoreApplication::processEvents();
  QVERIFY(chunks->isVisible());
  QVERIFY(hex_panel->isVisible());
  QVERIFY(inspector->isVisible());
  cell.pass(QStringLiteral("File/View identities, native Open/Close/Quit "
                           "shortcuts and visibility toggles retained their "
                           "frozen identities"));
}

void Wp607aNativeGuiGateTest::dockFloatReset() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A04");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));
  auto* chunks = window->findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* inspector =
      window->findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(chunks != nullptr);
  QVERIFY(inspector != nullptr);
  const auto check_features = [](QDockWidget* dock) {
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
  };
  check_features(chunks);
  check_features(inspector);

  chunks->setFloating(true);
  inspector->setFloating(true);
  QCoreApplication::processEvents();
  QVERIFY(chunks->isFloating());
  QVERIFY(inspector->isFloating());

  QAction* reset = nullptr;
  for (QAction* action : window->menuBar()->actions()) {
    if (action->menu() == nullptr ||
        action->menu()->title() != QStringLiteral("&View")) {
      continue;
    }
    for (QAction* child : action->menu()->actions()) {
      if (child->text() == QStringLiteral("&Reset Layout")) {
        reset = child;
      }
    }
  }
  QVERIFY(reset != nullptr);
  reset->trigger();
  QCoreApplication::processEvents();
  QVERIFY(!chunks->isFloating());
  QVERIFY(!inspector->isFloating());
  QCOMPARE(window->dockWidgetArea(chunks), Qt::LeftDockWidgetArea);
  QCOMPARE(window->dockWidgetArea(inspector), Qt::RightDockWidgetArea);
  QVERIFY(chunks->isVisible());
  QVERIFY(inspector->isVisible());
  QVERIFY(chunks->width() >= chunks->minimumWidth());
  QVERIFY(inspector->width() >= inspector->minimumWidth());
  QVERIFY(chunks->width() <= window->width());
  QVERIFY(inspector->width() <= window->width());
  cell.pass(QStringLiteral("both docks floated and Reset Layout restored "
                           "areas, visibility and bounded widths"));
}

void Wp607aNativeGuiGateTest::keyboardFocus() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A05");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));
  const QStringList required{
      QStringLiteral("xCoordinate"), QStringLiteral("yCoordinate"),
      QStringLiteral("lockCoordinate"), QStringLiteral("numericBase"),
      QStringLiteral("previewTabs"), QStringLiteral("hexSourceTabs"),
      QStringLiteral("inspectorTabs")};

  window->setFocus();
  window->activateWindow();
  QCoreApplication::processEvents();

  QStringList visited;
  QString previous_head;
  int stuck = 0;
  for (int step = 0; step < 60; ++step) {
    QTest::keyClick(window.get(), Qt::Key_Tab);
    QCoreApplication::processEvents();
    QWidget* focused = window->focusWidget();
    if (focused == nullptr || focused == window.get()) {
      ++stuck;
      continue;
    }
    const QStringList names = focusChainNames(window.get(), focused);
    const QString head = names.isEmpty() ? QString() : names.first();
    if (head == previous_head) {
      ++stuck;
    } else {
      stuck = 0;
      previous_head = head;
    }
    visited += names;
  }
  for (const QString& name : required) {
    QVERIFY2(visited.contains(name),
             qPrintable(QStringLiteral("Tab focus never reached %1")
                            .arg(name)));
  }
  // No trap: the traversal wrapped (a required control was reached again)
  // and focus kept moving between distinct controls.
  bool wrapped = false;
  for (const QString& name : required) {
    if (visited.count(name) >= 2) {
      wrapped = true;
      break;
    }
  }
  QVERIFY2(wrapped, "Tab traversal never wrapped around the full chain");
  QVERIFY2(stuck <= 5, "Tab focus got stuck on one control");

  // Shift-Tab returns to a previously visited control.
  QTest::keyClick(window.get(), Qt::Key_Tab, Qt::ShiftModifier);
  QCoreApplication::processEvents();
  QWidget* back = window->focusWidget();
  QVERIFY(back != nullptr);
  const QStringList back_names = focusChainNames(window.get(), back);
  QVERIFY(!back_names.isEmpty());
  QVERIFY2(visited.contains(back_names.first()),
           "Shift-Tab did not return to a previously visited control");
  cell.pass(QStringLiteral("real Tab/Shift-Tab events covered xCoordinate, "
                           "yCoordinate, lockCoordinate, numericBase, "
                           "previewTabs, hexSourceTabs and inspectorTabs; "
                           "traversal wrapped without a trap"));
}

void Wp607aNativeGuiGateTest::accessibleTree() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A06");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));

  QStringList snapshot;
  QStringList violations;
  // Menus and their named items (the actions surface as the menus' children).
  QMenu* file_menu = nullptr;
  QMenu* view_menu = nullptr;
  for (QAction* menu_action : window->menuBar()->actions()) {
    if (menu_action->menu() == nullptr) {
      continue;
    }
    if (menu_action->menu()->title() == QStringLiteral("&File")) {
      file_menu = menu_action->menu();
    } else if (menu_action->menu()->title() == QStringLiteral("&View")) {
      view_menu = menu_action->menu();
    }
  }
  requireMenuItems(QStringLiteral("fileMenu"), file_menu, &snapshot,
                   &violations);
  requireMenuItems(QStringLiteral("viewMenu"), view_menu, &snapshot,
                   &violations);

  // Controller ruling (fix round): the container name via the Chunks dock
  // pane (Pane role) satisfies the A06 observable semantics on Qt 6.11.1;
  // the production accessibleName change remains forbidden (R8).
  auto* chunks = window->findChild<QDockWidget*>(QStringLiteral("chunksDock"));
  auto* tree = window->findChild<QTreeView*>();
  requireAccessible(QStringLiteral("chunksDock"), chunks, QAccessible::Pane,
                    true, &snapshot, &violations);
  requireAccessible(QStringLiteral("chunkTree"), tree, QAccessible::Tree,
                    false, &snapshot, &violations);

  // Preview tabs, Hex controls, coordinate controls and the always-visible
  // status surfaces: expected role plus non-empty stable name, asserted at
  // the default state.
  auto* preview =
      window->findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  requireAccessible(QStringLiteral("previewTabs"), preview,
                    QAccessible::Client, true, &snapshot, &violations);
  requireAccessible(QStringLiteral("previewTabBar"),
                    preview != nullptr ? preview->tabBar() : nullptr,
                    QAccessible::PageTabList, true, &snapshot, &violations);
  requireAccessible(QStringLiteral("hexSourceTabs"),
                    window->findChild<QWidget*>(QStringLiteral("hexSourceTabs")),
                    QAccessible::PageTabList, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("hexView"),
      window->findChild<QWidget*>(QStringLiteral("hexView")),
      QAccessible::Client, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("xCoordinate"),
      window->findChild<QWidget*>(QStringLiteral("xCoordinate")),
      QAccessible::SpinBox, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("yCoordinate"),
      window->findChild<QWidget*>(QStringLiteral("yCoordinate")),
      QAccessible::SpinBox, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("lockCoordinate"),
      window->findChild<QWidget*>(QStringLiteral("lockCoordinate")),
      QAccessible::CheckBox, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("numericBase"),
      window->findChild<QWidget*>(QStringLiteral("numericBase")),
      QAccessible::Button, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("chunkDetailTable"),
      window->findChild<QWidget*>(QStringLiteral("chunkDetailTable")),
      QAccessible::Table, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("pixelStatus"),
      window->findChild<QLabel*>(QStringLiteral("pixelStatus")),
      QAccessible::StaticText, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("validationStatus"),
      window->findChild<QLabel*>(QStringLiteral("validationStatus")),
      QAccessible::StaticText, true, &snapshot, &violations);

  // Re-execution ruling 3: QAccessible marks inactive-tab-page widgets
  // invisible (correct accessibility semantics — hidden controls must not
  // be announced), so the snapshot activates each relevant tab page before
  // asserting that page's controls. Every required control is asserted in
  // its visible state across the activation sequence; no assertion is
  // removed, the previous cross-page over-assertion is corrected.
  auto* inspector_tabs =
      window->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  requireAccessible(QStringLiteral("inspectorTabs"), inspector_tabs,
                    QAccessible::Client, true, &snapshot, &violations);

  auto* compression_pages =
      window->findChild<QTabWidget*>(
          QStringLiteral("compressionInspectorPages"));
  QVERIFY(compression_pages != nullptr);

  inspector_tabs->setCurrentIndex(1);  // "Compression" container page.
  QCoreApplication::processEvents();
  requireAccessible(
      QStringLiteral("compressionInspectorPages"), compression_pages,
      QAccessible::Client, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("compressionContextStatus"),
      window->findChild<QLabel*>(QStringLiteral("compressionContextStatus")),
      QAccessible::StaticText, true, &snapshot, &violations);

  compression_pages->setCurrentIndex(0);  // "DEFLATE Blocks" page.
  QCoreApplication::processEvents();
  requireAccessible(
      QStringLiteral("blockInspector"),
      window->findChild<QWidget*>(QStringLiteral("blockInspector")),
      QAccessible::Client, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("blocksTable"),
      window->findChild<QTableView*>(QStringLiteral("compressionBlocksTable")),
      QAccessible::Table, true, &snapshot, &violations);

  compression_pages->setCurrentIndex(1);  // "Huffman" page.
  QCoreApplication::processEvents();
  requireAccessible(QStringLiteral("huffmanTable"),
                    window->findChild<QTableView*>(
                        QStringLiteral("compressionHuffmanTable")),
                    QAccessible::Table, true, &snapshot, &violations);

  compression_pages->setCurrentIndex(2);  // "Decode Trace" page.
  QCoreApplication::processEvents();
  requireAccessible(QStringLiteral("decodeTable"),
                    window->findChild<QTableView*>(
                        QStringLiteral("compressionDecodeTraceTable")),
                    QAccessible::Table, true, &snapshot, &violations);
  requireAccessible(
      QStringLiteral("compressionContextStatus"),
      window->findChild<QLabel*>(QStringLiteral("compressionContextStatus")),
      QAccessible::StaticText, true, &snapshot, &violations);

  if (!violations.isEmpty()) {
    QFAIL(qPrintable(QStringLiteral("QAccessible snapshot violations: %1")
                         .arg(violations.join(QStringLiteral("; ")))));
  }
  // Controller ruling (fix round): the record note states the chunk-tree
  // fallback explicitly AND the M04 escalation requirement — if VoiceOver
  // announces no usable name for the tree, the A06 disposition flips to
  // FAIL via the defect path (R9).
  const QString note =
      QStringLiteral("QAccessible snapshot, %1 entries carrying actual "
                     "role/name/state/value tokens: %2. Per re-execution "
                     "ruling, each inspector tab page (Compression "
                     "container, DEFLATE Blocks, Huffman, Decode Trace) "
                     "was activated before asserting its controls, so "
                     "every required control is asserted in its visible "
                     "state across the activation sequence. Chunk-tree "
                     "fallback: the chunk tree exposes the Tree role with "
                     "no own accessible name on this Qt build; its stable "
                     "name is the Chunks dock pane ('Chunks'). The M04 "
                     "VoiceOver manual check must record the actual "
                     "Chunk-tree announcement; if VoiceOver announces no "
                     "usable name for the tree, the A06 disposition flips "
                     "to FAIL through the defect path.")
          .arg(snapshot.size())
          .arg(snapshot.join(QStringLiteral("; ")));
  cell.pass(note);
}

void Wp607aNativeGuiGateTest::clipboard() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A07");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));
  auto* groups = window->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(groups != nullptr);
  groups->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* block_page = window->findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block_page != nullptr);
  const auto generation_before = block_page->view().generation;
  QVERIFY(generation_before != 0);

  bool found_selectable_value = false;
  for (const auto* label : block_page->findChildren<QLabel*>()) {
    if (label->textInteractionFlags() & Qt::TextSelectableByMouse) {
      found_selectable_value = true;
      break;
    }
  }
  QVERIFY(found_selectable_value);

  auto* clipboard = QApplication::clipboard();
  QVERIFY(clipboard != nullptr);
  clipboard->setText(QStringLiteral("pnga-wp607a-native-gate"));
  QCOMPARE(clipboard->text(), QStringLiteral("pnga-wp607a-native-gate"));
  QCOMPARE(block_page->view().generation, generation_before);
  auto* status = window->findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QVERIFY(status->text().contains(QStringLiteral("ready")));
  cell.pass(QStringLiteral("synthetic value round-trip per product-gate "
                           "precedent (5U12F) through the native clipboard; "
                           "analysis generation and ready state stayed "
                           "unchanged"));
}

void Wp607aNativeGuiGateTest::rapidSwitch() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A08");
  const QStringList cycle{
      QStringLiteral("ui-gray1-none"),
      QStringLiteral("ui-rgba16-byte-select"),
      QStringLiteral("error-truncated-token"),
      QStringLiteral("ui-rgb8-five-filters")};
  const auto final_fixture =
      fixtureById(QStringLiteral("ui-rgb8-five-filters"));
  auto window = shownWindow();
  for (int i = 0; i < 12; ++i) {
    const auto fixture = fixtureById(cycle.at(i % cycle.size()));
    QVERIFY(window->openFile(wp607a::fixturePath(fixture)));
    QCoreApplication::processEvents();
  }

  const QString final_name = QStringLiteral("ui-rgb8-five-filters.png");
  QVERIFY(window->windowTitle().contains(final_name));
  QVERIFY(!window->windowTitle().contains(QStringLiteral("ui-gray1-none.png")));
  QVERIFY(!window->windowTitle().contains(
      QStringLiteral("ui-rgba16-byte-select.png")));
  QVERIFY(!window->windowTitle().contains(
      QStringLiteral("error-truncated-token.png")));

  auto* status = window->findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           wp607a::kReadyTimeoutMs);
  auto* block = window->findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!block->view().rows.empty(),
                           wp607a::kReadyTimeoutMs);
  QVERIFY(block->view().generation != 0);
  auto* image = window->findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(),
                           wp607a::kReadyTimeoutMs);

  // Close/reopen responsiveness leg (package A08 wording): after the 12
  // alternations the document must still close cleanly and reopen into a
  // usable twelfth-generation document, under bounded waits.
  auto* close_action =
      window->findChild<QAction*>(QStringLiteral("closeImageAction"));
  QVERIFY(close_action != nullptr);
  close_action->trigger();
  QCoreApplication::processEvents();
  QTRY_VERIFY_WITH_TIMEOUT(!window->windowTitle().contains(final_name),
                           wp607a::kReadyTimeoutMs);
  QVERIFY(!close_action->isEnabled());
  QVERIFY(image->image().isNull());
  QCOMPARE(block->view().rows.size(), std::size_t{0});
  QVERIFY(window->openFile(wp607a::fixturePath(final_fixture)));
  QCoreApplication::processEvents();
  QTRY_VERIFY_WITH_TIMEOUT(close_action->isEnabled(), wp607a::kReadyTimeoutMs);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           wp607a::kReadyTimeoutMs);
  QTRY_VERIFY_WITH_TIMEOUT(!block->view().rows.empty(),
                           wp607a::kReadyTimeoutMs);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(),
                           wp607a::kReadyTimeoutMs);
  QVERIFY(window->windowTitle().contains(final_name));
  cell.pass(QStringLiteral("12 alternating valid/malformed opens published "
                           "only the twelfth generation "
                           "(ui-rgb8-five-filters.png) with ready context, "
                           "rows and image; close and reopen stayed "
                           "responsive and restored a usable document"));
}

void Wp607aNativeGuiGateTest::chunkToFileBytes() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A09");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));
  auto* tree = window->findChild<QTreeView*>();
  auto* model = window->findChild<pnga::ui::qt::ChunkModel*>();
  auto* hex = window->findChild<pnga::ui::qt::HexView*>(QStringLiteral("hexView"));
  auto* hex_source = window->findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  QVERIFY(tree != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(hex != nullptr);
  QVERIFY(hex_source != nullptr);
  QVERIFY(tree->model()->rowCount() > 1);

  quint64 ihdr_offset = 0;
  quint64 idat_offset = 0;
  for (const int row : {0, 1}) {
    const auto& node = model->chunkAt(row);
    tree->selectionModel()->setCurrentIndex(
        model->index(row, 0),
        QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(hex->currentLocation().value_or(1), node.header_offset,
                              wp607a::kReadyTimeoutMs);
    QVERIFY(hex->highlightCount() >= 1);
    QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
    if (row == 0) {
      ihdr_offset = node.header_offset;
    } else {
      idat_offset = node.header_offset;
    }
  }
  cell.pass(QStringLiteral("selecting the IHDR and IDAT rows navigated File "
                           "Hex to the exact chunk header offsets %1 and %2 "
                           "with envelope highlights")
                .arg(ihdr_offset)
                .arg(idat_offset));
}

void Wp607aNativeGuiGateTest::stageToPixel() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A10");
  auto window = shownWindow();
  openValidReady(*window, fixtureById(QStringLiteral("ui-rgb8-five-filters")));
  auto* x = window->findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window->findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window->findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  // Re-select the delivered pixel (1, 1): release an existing auto lock first
  // so the commit below is real.
  lock->setChecked(false);
  QCoreApplication::processEvents();
  x->setValue(1);
  y->setValue(1);
  lock->setChecked(true);

  auto* status = window->findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           wp607a::kReadyTimeoutMs);
  auto* stage = window->findChild<pnga::ui::qt::StageInspector*>(
      QStringLiteral("reconstructInspector"));
  QVERIFY(stage != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(stage->model()->hasData(), wp607a::kReadyTimeoutMs);
  QCOMPARE(stage->model()->pixelX(), std::uint64_t{1});
  QCOMPARE(stage->model()->pixelY(), std::uint64_t{1});

  const auto stores =
      window->findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  QVERIFY(store->state().current.has_value());

  // A manual Decode Trace row selection stays independent from Current.
  auto* groups = window->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* pages = window->findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
  QVERIFY(groups != nullptr);
  QVERIFY(pages != nullptr);
  groups->setCurrentIndex(1);
  pages->setCurrentIndex(2);
  QCoreApplication::processEvents();
  auto* decode = window->findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  auto* table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1,
                           wp607a::kReadyTimeoutMs);
  table->selectRow(0);
  QCoreApplication::processEvents();
  QVERIFY(store->state().manual.has_value());
  QVERIFY(store->state().current.has_value());
  cell.pass(QStringLiteral("selecting delivered pixel (1, 1) updated the "
                           "reconstruction report and Compression Current "
                           "context; a manual Decode Trace row selection "
                           "stayed independent"));
}

void Wp607aNativeGuiGateTest::pixelToTokenBits() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  wp607a::CellScope cell(&cells_, "A11");
  auto window = shownWindow();
  openValidReady(
      *window, fixtureById(QStringLiteral("trace-dynamic-overlap-repeats")));
  auto* x = window->findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window->findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window->findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  lock->setChecked(false);
  QCoreApplication::processEvents();
  x->setValue(0);
  y->setValue(0);
  lock->setChecked(true);

  auto* groups = window->findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  auto* pages = window->findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
  QVERIFY(groups != nullptr);
  QVERIFY(pages != nullptr);
  groups->setCurrentIndex(1);
  pages->setCurrentIndex(2);
  QCoreApplication::processEvents();
  auto* decode = window->findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  auto* table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  auto* context_status = window->findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);

  // Bounded Trace under a 10-second QtTest deadline; a timeout is FAIL with
  // the last visible status in the note (R9).
  if (!QTest::qWaitFor(
          [&table]() { return table->model()->rowCount() >= 1; },
          wp607a::kTraceTimeoutMs)) {
    cell.fail(QStringLiteral("bounded Trace did not publish rows within "
                             "10 s; last visible status: ") +
              context_status->text());
    QFAIL("A11: bounded Trace did not publish within the 10 s deadline");
  }

  int match_row = -1;
  pnga::analysis_engine::DecodeTraceStep step;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const auto candidate =
        table->model()
            ->data(table->model()->index(row, 0),
                   pnga::ui::qt::DecodeTraceStepRole)
            .value<pnga::analysis_engine::DecodeTraceStep>();
    if (candidate.path == pnga::analysis_engine::DecodeTracePath::kMatch &&
        !candidate.physical_input_spans.empty()) {
      match_row = row;
      step = candidate;
      break;
    }
  }
  if (match_row < 0) {
    cell.fail(QStringLiteral("no Match step with physical input spans in the "
                             "bounded trace; last visible status: ") +
              context_status->text());
    QFAIL("A11: no Match step with physical input spans was published");
  }

  const auto stores =
      window->findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  auto* hex = window->findChild<pnga::ui::qt::HexView*>(QStringLiteral("hexView"));
  auto* hex_source = window->findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  QVERIFY(hex != nullptr);
  QVERIFY(hex_source != nullptr);
  table->selectRow(match_row);
  auto* hex_button =
      decode->findChild<QPushButton*>(QStringLiteral("decodeShowInHex"));
  QVERIFY(hex_button != nullptr);
  QVERIFY(hex_button->isEnabled());
  hex_button->click();
  QCoreApplication::processEvents();

  QVERIFY(!store->history().empty());
  const auto& target = store->history().back();
  const auto* bits =
      std::get_if<pnga::trace_model::DeflateBitRange>(&target.logical_range);
  QVERIFY(bits != nullptr);
  QCOMPARE(bits->begin, step.input_range.begin);
  QCOMPARE(bits->end, step.input_range.end);
  QCOMPARE(target.physical_spans, step.physical_input_spans);
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(hex->highlightCount(), step.physical_input_spans.size());
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(),
           step.physical_input_spans.front().begin.value);
  cell.pass(QStringLiteral("Match token %1 carried DeflateBitRange "
                           "[%2, %3) mapped to %4 physical File spans in Hex")
                .arg(step.token_index)
                .arg(bits->begin.value)
                .arg(bits->end.value)
                .arg(step.physical_input_spans.size()));
}

void Wp607aNativeGuiGateTest::writeEvidence() {
  if (wp607a::nonNativePlatform()) {
    QSKIP("offscreen/minimal: WP-607A native gate records no evidence (R9)");
  }
  QStringList violations;
  QSet<QString> seen;
  for (const auto& result : cells_) {
    if (!wp607a::automatedCells().contains(result.id)) {
      violations << QStringLiteral("unknown cell %1").arg(result.id);
    } else if (seen.contains(result.id)) {
      violations << QStringLiteral("duplicate cell %1").arg(result.id);
    }
    seen.insert(result.id);
  }
  for (const QString& id : wp607a::automatedCells()) {
    if (!seen.contains(id)) {
      violations << QStringLiteral("missing cell %1").arg(id);
    }
  }
  if (!violations.isEmpty()) {
    QFAIL(qPrintable(QStringLiteral("refusing to write evidence: %1")
                         .arg(violations.join(QStringLiteral("; ")))));
  }
  const auto record = wp607a::buildRecord(fixtures_, cells_);
  const QString from_env = qEnvironmentVariable("PNGA_WP607A_OUT");
  const QString root = from_env.isEmpty()
                           ? QStringLiteral("build/evidence/wp-607a")
                           : from_env;
  QVERIFY(QDir().mkpath(root));
  const QString path = QDir(root).filePath(QStringLiteral("automated.json"));
  QFile output(path);
  QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
  const QByteArray payload = QJsonDocument(record).toJson(QJsonDocument::Compact);
  QCOMPARE(output.write(payload), payload.size());
  QCOMPARE(output.write("\n"), 1);
  output.close();
  qInfo().noquote() << QStringLiteral("wp607a: automated.json written with %1 "
                                      "cells at relative out root")
                           .arg(cells_.size());
}

QTEST_MAIN(Wp607aNativeGuiGateTest)
#include "wp_607a_native_gui_gate_test.moc"
