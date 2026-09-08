// WP-706: GUI-side performance baselines for the APNG timeline and playback.
// The headless runner cannot measure Qt main-thread behavior, so this test
// drives the real AnimationTimelineWidget and MainWindow offscreen and
// records:
//   - timeline model population plus a full scroll sweep across 100,000
//     entries (per-step event-loop time, p50/p95);
//   - main-thread blocking during real playback of a 100-frame document,
//     sampled as the maximum gap between 1 ms event-loop ticks (the standard
//     responsiveness probe; a handler chain that blocks the loop shows up
//     as an elongated gap).
// The plan approves no APNG time thresholds, so the measurements are only
// recorded: printed on every run and, when PNGA_APNG_PERF_OUT points at a
// directory, written to apng-gui-performance.json with environment fields
// for the completion record. Assertions are hang-guards only.

#include "main_window.h"

#include "apng_fixture.h"

#include <pnga/analysis-engine/animation_playback.h>
#include <pnga/ui/qt/animation_timeline.h>
#include <pnga/ui/qt/animation_timeline_model.h>

#include <QtTest/QtTest>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pnga::analysis_engine::AnimationTimeline;
using pnga::analysis_engine::TimelineEntry;

constexpr std::uint64_t kSeed = 20260908;

std::uint64_t percentile(std::vector<std::uint64_t> values,
                         std::uint64_t numerator) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::uint64_t rank =
      (static_cast<std::uint64_t>(values.size()) * numerator + 99) / 100;
  return values[static_cast<std::size_t>(
      std::min<std::uint64_t>(rank == 0 ? 1 : rank, values.size())) - 1];
}

class ApngGuiPerformanceTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();

  // Baseline: populate the real timeline widget with 100,000 entries and
  // sweep the scroll range, recording per-scroll-step event-loop cost.
  void timelineModelScrollBaseline();

  // Baseline: play a 100-frame document in a real MainWindow and record the
  // 1 ms event-loop tick gaps (main-thread blocking) plus per-frame
  // publication interval.
  void playbackMainThreadBlockingBaseline();

 private:
  static void write_record(const QJsonObject& environment);
  static QJsonObject environment_;
  QJsonArray cells_;
};

QJsonObject ApngGuiPerformanceTest::environment_ = {};

void ApngGuiPerformanceTest::initTestCase() {
  environment_ = QJsonObject{
      {QStringLiteral("schema"),
       QStringLiteral("pnga-apng-gui-performance-v1")},
      {QStringLiteral("os"),
       QSysInfo::productType() + QLatin1Char(' ') + QSysInfo::productVersion()},
      {QStringLiteral("arch"), QSysInfo::currentCpuArchitecture()},
      {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
      {QStringLiteral("qpa_platform"), QGuiApplication::platformName()},
      {QStringLiteral("timestamp_utc"),
       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {QStringLiteral("git_commit"),
       qEnvironmentVariable("PNGA_APNG_PERF_COMMIT")},
  };
}

void ApngGuiPerformanceTest::write_record(const QJsonObject& environment) {
  const QString out = qEnvironmentVariable("PNGA_APNG_PERF_OUT");
  if (out.isEmpty()) {
    return;
  }
  QFile file(out + QStringLiteral("/apng-gui-performance.json"));
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "gui performance record write failed");
  file.write(QJsonDocument(environment).toJson(QJsonDocument::Indented));
  file.close();
}

void ApngGuiPerformanceTest::timelineModelScrollBaseline() {
  const qint64 kEntries = 100000;
  AnimationTimeline timeline;
  timeline.complete = true;
  timeline.entries.reserve(static_cast<std::size_t>(kEntries));
  for (qint64 i = 0; i < kEntries; ++i) {
    timeline.entries.push_back(TimelineEntry{static_cast<std::uint32_t>(i), 1,
                                             100,
                                             static_cast<std::uint64_t>(i) *
                                                 10000000ull,
                                             10000000ull});
  }

  QWidget host;
  auto* layout = new QVBoxLayout(&host);
  pnga::ui::qt::AnimationTimelineWidget widget;
  layout->addWidget(&widget);
  widget.resize(1100, 120);
  host.resize(1100, 140);
  host.show();

  QElapsedTimer timer;
  timer.start();
  widget.setTimeline(timeline);
  const auto populate_us = static_cast<std::uint64_t>(timer.nsecsElapsed() / 1000);

  auto* list = widget.findChild<QListView*>(QStringLiteral("animationThumbnails"));
  QVERIFY(list != nullptr);
  auto* bar = list->verticalScrollBar();
  QVERIFY(bar != nullptr);
  bar->setRange(0, 100000);

  std::vector<std::uint64_t> step_us;
  step_us.reserve(1000);
  const int steps = 1000;
  timer.restart();
  for (int i = 0; i < steps; ++i) {
    const auto step_start = timer.nsecsElapsed();
    bar->setValue(bar->minimum() +
                  (bar->maximum() - bar->minimum()) * i / (steps - 1));
    // Force the event loop to service the scroll and paint scheduled work.
    QCoreApplication::processEvents();
    step_us.push_back(
        static_cast<std::uint64_t>((timer.nsecsElapsed() - step_start) / 1000));
  }
  const auto sweep_us = static_cast<std::uint64_t>(timer.nsecsElapsed() / 1000);

  QJsonObject cell{
      {QStringLiteral("cell"), QStringLiteral("timeline-scroll")},
      {QStringLiteral("entries"), kEntries},
      {QStringLiteral("populate_us"), double(populate_us)},
      {QStringLiteral("scroll_steps"), steps},
      {QStringLiteral("sweep_us"), double(sweep_us)},
      {QStringLiteral("step_p50_us"), double(percentile(step_us, 50))},
      {QStringLiteral("step_p95_us"), double(percentile(step_us, 95))},
      {QStringLiteral("step_max_us"), double(percentile(step_us, 100))},
      {QStringLiteral("result"), QStringLiteral("recorded")},
  };
  QVERIFY(sweep_us < 60000000);  // hang guard, not a performance threshold
  cells_.append(cell);
  environment_.insert(QStringLiteral("cells"), cells_);
  write_record(environment_);
  qInfo() << "timeline-scroll baseline: entries" << kEntries << "populate_us"
          << populate_us << "sweep_us" << sweep_us << "step_p50_us"
          << percentile(step_us, 50) << "step_p95_us" << percentile(step_us, 95)
          << "step_max_us" << percentile(step_us, 100);
}

void ApngGuiPerformanceTest::playbackMainThreadBlockingBaseline() {
  // 100-frame 96x64 document, full-canvas SOURCE frames.
  constexpr std::size_t kFrames = 100;
  std::vector<pnga::png_format::FrameControl> controls(kFrames);
  std::vector<std::array<std::byte, 4>> colors(kFrames);
  for (std::size_t i = 0; i < kFrames; ++i) {
    controls[i] = pnga::png_format::FrameControl{
        0, 96, 64, 0, 0, 1, 1, 0, 0};  // 10 ms effective delay at 1x
    const auto r = static_cast<unsigned char>((i * 29 + 40) % 256);
    const auto g = static_cast<unsigned char>((i * 53 + 80) % 256);
    const auto b = static_cast<unsigned char>((i * 7 + 160) % 256);
    colors[i] = {std::byte{r}, std::byte{g}, std::byte{b}, std::byte{255}};
  }
  const auto png =
      pnga_test::make_apng_canvas(false, controls, colors, 96, 64);
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("perf.apng"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(reinterpret_cast<const char*>(png.data()),
                      static_cast<qint64>(png.size())),
           static_cast<qint64>(png.size()));
  file.flush();
  file.close();

  MainWindow window;
  window.resize(1400, 950);
  QVERIFY(window.openFile(path));
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(
      window.findChild<QWidget*>(QStringLiteral("animationStage0")) != nullptr,
      10000);
  auto* controller = window.findChild<AnimationController*>();
  QVERIFY(controller != nullptr);

  // 1 ms event-loop probe: playback work on the main thread (frame
  // publication, presentAnimationFrame, pixel copies) shows up as
  // elongated gaps between consecutive ticks.
  QElapsedTimer probe_clock;
  probe_clock.start();
  std::vector<std::uint64_t> gaps;
  gaps.reserve(20000);
  QTimer probe;
  probe.setInterval(1);
  qint64 last = 0;

  // 8 s time-bounded sampling window: the metric is the gap distribution,
  // not a publication count, so the window closes on a timer and the
  // publication count is only sanity-checked afterwards.
  int publications = 0;
  bool window_closed = false;
  QObject::connect(controller, &AnimationController::framePublished,
                   [&publications]() { ++publications; });
  QObject::connect(&probe, &QTimer::timeout, [&] {
    const qint64 now = probe_clock.nsecsElapsed();
    gaps.push_back(static_cast<std::uint64_t>((now - last) / 1000));
    last = now;
  });
  QTimer::singleShot(8000, &window, [&] {
    probe.stop();
    window_closed = true;
  });

  probe.start();
  window.findChild<QPushButton*>(QStringLiteral("animationPlay"))->click();
  QTRY_VERIFY_WITH_TIMEOUT(window_closed, 30000);
  window.findChild<QPushButton*>(QStringLiteral("animationPlay"))->click();

  qInfo() << "playback probe: publications" << publications
          << "gaps" << gaps.size() << "gap_p50_us"
          << percentile(gaps, 50) << "gap_p95_us" << percentile(gaps, 95)
          << "gap_max_us" << percentile(gaps, 100);
  // Throughput on the first pass is worker-bound (each replayed frame pays
  // a full stage decode); the gap distribution is the blocking metric.
  QVERIFY(publications > 3);
  QVERIFY(gaps.size() > 100);
  QJsonObject cell{
      {QStringLiteral("cell"), QStringLiteral("playback-main-thread")},
      {QStringLiteral("frames"), double(kFrames)},
      {QStringLiteral("publications"), publications},
      {QStringLiteral("probe_interval_ms"), 1},
      {QStringLiteral("gap_p50_us"), double(percentile(gaps, 50))},
      {QStringLiteral("gap_p95_us"), double(percentile(gaps, 95))},
      {QStringLiteral("gap_max_us"), double(percentile(gaps, 100))},
      {QStringLiteral("result"), QStringLiteral("recorded")},
  };
  cells_.append(cell);
  environment_.insert(QStringLiteral("cells"), cells_);
  write_record(environment_);
  qInfo() << "playback blocking baseline: publications" << publications
          << "gap_p50_us" << percentile(gaps, 50) << "gap_p95_us"
          << percentile(gaps, 95) << "gap_max_us" << percentile(gaps, 100);
}

}  // namespace

QTEST_MAIN(ApngGuiPerformanceTest)
#include "apng_gui_performance_test.moc"
