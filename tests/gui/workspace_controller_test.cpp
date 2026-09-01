// WP-5U15 Task 4: workspace defaults, settings keys and recent-file behavior
// must keep their existing keys, caps and menu semantics after extraction.

#include "workspace_controller.h"

#include <QtTest/QtTest>

#include <QFile>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>

class WorkspaceControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void defaultsAndRecentFilesKeepExistingKeys();
};

void WorkspaceControllerTest::defaultsAndRecentFilesKeepExistingKeys() {
  QSettings settings;
  settings.clear();
  QMainWindow window;
  // Child-dock isVisible() is only true once the top-level window is shown,
  // so exhibit the window before asserting the default dock visibility the
  // facade provides after applyDefaults().
  window.show();
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  QString opened;
  WorkspaceController workspace(window, widgets,
      [&opened](const QString& path) { opened = path; });
  workspace.applyDefaults();
  QCOMPARE(window.minimumSize(), QSize(840, 520));
  QCOMPARE(window.size(), QSize(1200, 760));
  QCOMPARE(widgets.preview_tabs->currentIndex(), 0);
  QVERIFY(widgets.chunks_dock->isVisible());
  QVERIFY(widgets.inspector_dock->isVisible());

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for (int i = 0; i < 11; ++i) {
    const QString path = dir.filePath(QStringLiteral("i%1.png").arg(i));
    // refreshRecentFilesMenu() drops entries whose file no longer exists, so
    // the remembered paths must exist like every real opened document.
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("not a png") > 0);
    file.close();
    workspace.rememberOpenedFile(path);
  }
  const QStringList recent =
      settings.value(QStringLiteral("file/recentFiles")).toStringList();
  QCOMPARE(recent.size(), 10);
  QVERIFY(settings.contains(QStringLiteral("file/lastOpenDirectory")));
  QVERIFY(settings.contains(QStringLiteral("file/lastOpenFile")));
  workspace.refreshRecentFilesMenu();
  QCOMPARE(widgets.recent_files_menu->actions().size(), 10);
  widgets.recent_files_menu->actions().front()->trigger();
  QCOMPARE(opened, recent.front());
}

QTEST_MAIN(WorkspaceControllerTest)
#include "workspace_controller_test.moc"
