// png-analyzer-gui — desktop entry point (WP-001 walking skeleton).
//
// Shows an empty main window whose title carries the shared core version.
// No PNG parsing or decoding happens here and must never happen on the UI
// thread (AGENTS.md); later milestones render analysis models only.

#include <pnga/core/version.h>

#include <QApplication>
#include <QMainWindow>
#include <QString>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("png-analyzer"));

  QMainWindow window;
  window.setWindowTitle(QStringLiteral("PNG Analyzer %1")
                            .arg(QString::fromLatin1(pnga::version_string())));
  window.resize(960, 640);
  window.show();

  return app.exec();
}