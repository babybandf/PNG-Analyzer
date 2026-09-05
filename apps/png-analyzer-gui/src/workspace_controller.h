#ifndef PNG_ANALYZER_GUI_WORKSPACE_CONTROLLER_H
#define PNG_ANALYZER_GUI_WORKSPACE_CONTROLLER_H

// WP-5U15: QSettings-backed workspace persistence, layout defaults/restore/
// save/reset and recent-file behavior, moved verbatim from the facade. The
// controller receives explicit widget/action references and never looks up
// arbitrary children. Every organization/application name, settings key,
// migration branch, recent-file cap and default size is preserved.

#include "main_window_ui.h"

#include <pnga/ui/qt/selection_view_state.h>

#include <QMainWindow>
#include <QString>

#include <functional>

class WorkspaceController final {
 public:
  using OpenRecentFile = std::function<void(const QString&)>;
  WorkspaceController(QMainWindow& window, MainWindowWidgets widgets,
                      OpenRecentFile open_recent_file);

  void restore();
  void save() const;
  void applyDefaults();
  void configureDockInteraction();
  // Ends the frozen drag state a dock keeps after the dock drag machinery
  // released it floating: QMainWindowLayout::savedState stays valid, which
  // makes QMainWindow::setGeometry() a no-op until a successful plug clears
  // it. The public-API saveState/restoreState round-trip clears that state
  // and re-applies the current layout, so redock paths position the dock
  // again. Called by Reset Layout and by the dock title-bar event filter.
  void normalize_frozen_dock_state();
  void refreshRecentFilesMenu();
  void rememberOpenedFile(const QString& path);
  void rememberLastOpenDirectory(const QString& path);
  QString lastOpenDirectory() const;
  QString lastOpenFile() const;

  // Single SelectionViewState owner while the selection/navigation code still
  // lives in the facade; the Task 6 extraction hands ownership to the
  // selection-navigation controller. The facade accesses the same state.
  pnga::ui::qt::SelectionViewState& viewState() noexcept {
    return view_state_;
  }

 private:
  void updateNumericBaseButton();

  QMainWindow& window_;
  MainWindowWidgets w_;
  OpenRecentFile open_recent_file_;
  pnga::ui::qt::SelectionViewState view_state_;
};

#endif  // PNG_ANALYZER_GUI_WORKSPACE_CONTROLLER_H
