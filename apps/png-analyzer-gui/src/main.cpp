// png-analyzer-gui — desktop entry point (WP-001, WP-104).
//
// WP-104 shell: a MainWindow with a Chunk tree and a windowed Hex view.
// Optional first positional argument opens a PNG on startup. No PNG parsing or
// decoding happens on the UI thread (AGENTS.md); the chunk index is built
// synchronously on open (O(chunks), zero-copy) and the hex view only reads the
// visible window.

#include <pnga/core/version.h>

#include <QApplication>
#include <QString>

#include "main_window.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("png-analyzer"));

  MainWindow window;
  window.setWindowTitle(
      QStringLiteral("PNG Analyzer %1")
          .arg(QString::fromLatin1(pnga::version_string())));
  window.resize(1080, 720);
  window.show();

  if (argc > 1) {
    window.openFile(QString::fromLocal8Bit(argv[1]));
  }

  return app.exec();
}
