// png-analyzer-gui — desktop entry point (WP-001, WP-104).
//
// WP-104 shell: a MainWindow with a Chunk tree and a windowed Hex view.
// Optional first positional argument opens a PNG on startup. No PNG parsing or
// decoding happens on the UI thread (AGENTS.md); the chunk index is built
// synchronously on open (O(chunks), zero-copy) and the hex view only reads the
// visible window.

#include <pnga/core/version.h>
#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/application_theme.h>

#include <QApplication>
#include <QString>

#include "main_window.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("png-analyzer"));
  app.setWindowIcon(pnga::ui::qt::application_icon());

  pnga::ui::qt::ApplicationTheme theme(&app);
  theme.install();
  MainWindow window(nullptr, &theme);
  window.setWindowTitle(
      QStringLiteral("PNG Analyzer %1")
          .arg(QString::fromLatin1(pnga::version_string())));
  window.show();

  if (argc > 1) {
    window.openFile(QString::fromLocal8Bit(argv[1]));
  }

  return app.exec();
}
