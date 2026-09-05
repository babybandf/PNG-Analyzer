// WP-5U15: workspace/settings/recent-file bodies moved verbatim from
// main_window.cpp. The document-dependent hex data refresh (facade
// updateHexSource) stays in MainWindow; everything here is widget-local or
// QSettings-backed so standalone tests can drive it without a document.

#include "workspace_controller.h"

#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/stage_inspector.h>
#include <pnga/ui/qt/stage_pixel_process_view.h>

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QMenu>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>

namespace {

constexpr int kMaxRecentFiles = 10;
constexpr auto kRecentFilesSettingsKey = "file/recentFiles";
constexpr auto kLastOpenDirectorySettingsKey = "file/lastOpenDirectory";
constexpr auto kLastOpenFileSettingsKey = "file/lastOpenFile";

QString previewPageId(int index) {
  switch (index) {
    case 0:
      return QStringLiteral("image");
    case 1:
      return QStringLiteral("pixels");
    case 2:
      return QStringLiteral("filtered");
    case 3:
      return QStringLiteral("defiltered");
    default:
      return QString();
  }
}

int previewIndexForId(const QString& id) {
  if (id == QStringLiteral("image")) {
    return 0;
  }
  if (id == QStringLiteral("pixels")) {
    return 1;
  }
  if (id == QStringLiteral("filtered")) {
    return 2;
  }
  if (id == QStringLiteral("defiltered")) {
    return 3;
  }
  return 0;
}

QString inspectorPageId(int index) {
  switch (index) {
    case 0:
      return QStringLiteral("reconstruction");
    case 1:
      return QStringLiteral("compression");
    default:
      return QString();
  }
}

int inspectorIndexForId(const QString& id) {
  return id == QStringLiteral("compression") ? 1 : 0;
}

int migratePreviewIndexV1(int index) {
  switch (index) {
    case 1:
      return 1;  // Pixels
    case 3:
      return 2;  // Filtered
    case 4:
      return 3;  // Defiltered
    case 0:
    case 2:
    default:
      return 0;  // Image and the retired Filter Map
  }
}

int migrateInspectorIndexV1(int index) {
  return index == 2 ? 1 : 0;
}

}  // namespace

WorkspaceController::WorkspaceController(QMainWindow& window,
                                         MainWindowWidgets widgets,
                                         OpenRecentFile open_recent_file)
    : window_(window),
      w_(widgets),
      open_recent_file_(std::move(open_recent_file)) {}

void WorkspaceController::normalize_frozen_dock_state() {
  window_.restoreState(window_.saveState());
}

void WorkspaceController::applyDefaults() {
  const auto preserved_locked = view_state_.locked;
  const auto preserved_hover = view_state_.hover;
  // Reset Layout must restore the dock topology as well as its dimensions.
  // setFloating(false) re-docks through Qt's plug-back path and
  // addDockWidget() pins each dock to its normative side. A dock that the
  // drag machinery left floating keeps QMainWindowLayout::savedState valid,
  // which freezes main-window setGeometry() calls, so the normalization
  // below ends that frozen drag state and applies the re-docked layout
  // geometry before the normative dimensions are set.
  w_.chunks_dock->setFloating(false);
  window_.addDockWidget(Qt::LeftDockWidgetArea, w_.chunks_dock);
  w_.inspector_dock->setFloating(false);
  window_.addDockWidget(Qt::RightDockWidgetArea, w_.inspector_dock);
  normalize_frozen_dock_state();
  window_.resize(1200, 760);
  w_.center_splitter->setSizes({456, 304});
  w_.chunks_splitter->setSizes({360, 180});
  w_.preview_tabs->setCurrentIndex(0);
  w_.inspector_tabs->setCurrentIndex(0);
  w_.compression_inspector_tabs->setCurrentIndex(0);
  w_.chunks_dock->show();
  w_.inspector_dock->show();
  w_.chunks_dock->setMinimumWidth(180);
  w_.inspector_dock->setMinimumWidth(260);
  window_.resizeDocks({w_.chunks_dock, w_.inspector_dock}, {240, 420},
                      Qt::Horizontal);
  window_.setMinimumSize(840, 520);
  for (auto* splitter : window_.findChildren<QSplitter*>()) {
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
  }
  configureDockInteraction();

  view_state_.hex_source = pnga::ui::qt::HexSource::kFile;
  view_state_.numeric_base = pnga::ui::qt::NumericBase::kDecimal;
  view_state_.locked = preserved_locked;
  view_state_.hover = preserved_hover;
  {
    const QSignalBlocker base_blocker(w_.base_button);
    const QSignalBlocker lock_blocker(w_.lock_check);
    updateNumericBaseButton();
    w_.lock_check->setChecked(preserved_locked.has_value());
    if (preserved_locked.has_value()) {
      w_.x_spin->setValue(static_cast<int>(preserved_locked->x));
      w_.y_spin->setValue(static_cast<int>(preserved_locked->y));
    }
  }
  w_.hex_source_tabs->setSource(pnga::ui::qt::HexSource::kFile);
  w_.inspector->setNumericBase(false);
  w_.pixel_view->setNumericBase(false);
  w_.filtered_view->setNumericBase(false);
  w_.defiltered_view->setNumericBase(false);
}

void WorkspaceController::configureDockInteraction() {
  if (w_.chunks_dock == nullptr || w_.inspector_dock == nullptr) {
    return;
  }
  w_.chunks_dock->setMinimumWidth(180);
  w_.inspector_dock->setMinimumWidth(260);
  w_.chunks_dock->setSizePolicy(QSizePolicy::Preferred,
                                QSizePolicy::Expanding);
  w_.inspector_dock->setSizePolicy(QSizePolicy::Preferred,
                                   QSizePolicy::Expanding);
  // The internal QMainWindow dock layout is not exposed as QSplitter
  // children.  Re-polish after the first layout pass so the style-sheet
  // separator extent is applied to the actual native separators as well.
  QMetaObject::invokeMethod(
      &window_,
      [this] {
        if (w_.chunks_dock == nullptr || w_.inspector_dock == nullptr) {
          return;
        }
        w_.chunks_dock->updateGeometry();
        w_.inspector_dock->updateGeometry();
        window_.style()->unpolish(&window_);
        window_.style()->polish(&window_);
        window_.updateGeometry();
      },
      Qt::QueuedConnection);
}

void WorkspaceController::restore() {
  QSettings settings;
  const int workspace_version =
      settings.value(QStringLiteral("workspace/version")).toInt();
  const bool has_saved =
      (workspace_version == 1 || workspace_version == 2) &&
      settings.contains(QStringLiteral("workspace/geometry")) &&
      settings.contains(QStringLiteral("workspace/mainState")) &&
      settings.contains(QStringLiteral("workspace/splitterState"));
  if (!has_saved ||
      !window_.restoreGeometry(settings.value(QStringLiteral("workspace/geometry"))
                            .toByteArray()) ||
      !window_.restoreState(settings.value(QStringLiteral("workspace/mainState"))
                         .toByteArray()) ||
      !w_.center_splitter->restoreState(
          settings.value(QStringLiteral("workspace/splitterState"))
              .toByteArray())) {
    applyDefaults();
    return;
  }
  if (settings.contains(QStringLiteral("workspace/chunkDetailSplitterState"))) {
    w_.chunks_splitter->restoreState(
        settings.value(QStringLiteral("workspace/chunkDetailSplitterState"))
            .toByteArray());
  }

  int preview_index = 0;
  int inspector_index = 0;
  if (workspace_version == 2) {
    preview_index = previewIndexForId(
        settings.value(QStringLiteral("workspace/previewTabId"),
                       QStringLiteral("image"))
            .toString());
    inspector_index = inspectorIndexForId(
        settings.value(QStringLiteral("workspace/inspectorPageId"),
                       QStringLiteral("reconstruction"))
            .toString());
  } else {
    preview_index = migratePreviewIndexV1(
        settings.value(QStringLiteral("workspace/previewTab"), 0).toInt());
    inspector_index = migrateInspectorIndexV1(
        settings.value(QStringLiteral("workspace/inspectorTab"), 0).toInt());
  }
  w_.preview_tabs->setCurrentIndex(preview_index);
  w_.inspector_tabs->setCurrentIndex(inspector_index);
  const int compression_page = settings.value(QStringLiteral("workspace/compressionPage"), 0).toInt();
  if (compression_page >= 0 &&
      compression_page < w_.compression_inspector_tabs->count()) {
    w_.compression_inspector_tabs->setCurrentIndex(compression_page);
  }

  const int base = settings.value(QStringLiteral("view/numericBase"), 0).toInt();
  const int source = settings.value(QStringLiteral("view/hexSource"), 0).toInt();
  view_state_.numeric_base = base == 1
                                 ? pnga::ui::qt::NumericBase::kHexadecimal
                                 : pnga::ui::qt::NumericBase::kDecimal;
  view_state_.hex_source = source >= 0 && source <= 3
                                 ? static_cast<pnga::ui::qt::HexSource>(source)
                                 : pnga::ui::qt::HexSource::kFile;
  {
    const QSignalBlocker base_blocker(w_.base_button);
    updateNumericBaseButton();
  }
  w_.hex_source_tabs->setSource(view_state_.hex_source);
  w_.inspector->setNumericBase(base == 1);
  w_.pixel_view->setNumericBase(base == 1);
  w_.filtered_view->setNumericBase(base == 1);
  w_.defiltered_view->setNumericBase(base == 1);
}

void WorkspaceController::save() const {
  QSettings settings;
  settings.setValue(QStringLiteral("workspace/version"), 2);
  settings.setValue(QStringLiteral("workspace/geometry"),
                    window_.saveGeometry());
  settings.setValue(QStringLiteral("workspace/mainState"), window_.saveState());
  settings.setValue(QStringLiteral("workspace/splitterState"),
                    w_.center_splitter->saveState());
  settings.setValue(QStringLiteral("workspace/chunkDetailSplitterState"),
                    w_.chunks_splitter->saveState());
  settings.setValue(QStringLiteral("workspace/previewTabId"),
                    previewPageId(w_.preview_tabs->currentIndex()));
  settings.setValue(QStringLiteral("workspace/inspectorPageId"),
                    inspectorPageId(w_.inspector_tabs->currentIndex()));
  settings.setValue(QStringLiteral("workspace/compressionPage"),
                    w_.compression_inspector_tabs->currentIndex());
  settings.setValue(QStringLiteral("view/numericBase"),
                    static_cast<int>(view_state_.numeric_base));
  settings.setValue(QStringLiteral("view/hexSource"),
                    static_cast<int>(view_state_.hex_source));
}

void WorkspaceController::refreshRecentFilesMenu() {
  if (w_.recent_files_menu == nullptr) {
    return;
  }

  QSettings settings;
  const QStringList stored =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();
  QStringList valid;
  valid.reserve(kMaxRecentFiles);
  for (const QString& stored_path : stored) {
    if (valid.size() >= kMaxRecentFiles) {
      break;
    }
    if (stored_path.isEmpty()) {
      continue;
    }
    const QFileInfo info(stored_path);
    const QString path = info.absoluteFilePath();
    if (!info.exists() || !info.isFile() || valid.contains(path)) {
      continue;
    }
    valid.push_back(path);
  }
  if (valid != stored) {
    settings.setValue(QLatin1String(kRecentFilesSettingsKey), valid);
  }

  w_.recent_files_menu->clear();
  if (valid.isEmpty()) {
    QAction* empty = w_.recent_files_menu->addAction(
        QStringLiteral("No Recent Files"));
    empty->setObjectName(QStringLiteral("noRecentFiles"));
    empty->setEnabled(false);
    return;
  }

  for (const QString& path : valid) {
    const QFileInfo info(path);
    const QString label = info.fileName().isEmpty()
                              ? QDir::toNativeSeparators(path)
                              : info.fileName();
    QAction* action = w_.recent_files_menu->addAction(label);
    action->setData(path);
    action->setToolTip(QDir::toNativeSeparators(path));
    QObject::connect(action, &QAction::triggered, &window_,
                     [this, path] { open_recent_file_(path); });
  }
}

void WorkspaceController::rememberLastOpenDirectory(const QString& path) {
  const QFileInfo info(path);
  const QString directory = info.absolutePath();
  if (!directory.isEmpty() && QDir(directory).exists()) {
    QSettings settings;
    settings.setValue(QLatin1String(kLastOpenDirectorySettingsKey), directory);
    if (info.isFile()) {
      settings.setValue(QLatin1String(kLastOpenFileSettingsKey),
                        info.absoluteFilePath());
    }
    settings.sync();
  }
}

QString WorkspaceController::lastOpenDirectory() const {
  QSettings settings;
  const QString directory =
      settings.value(QLatin1String(kLastOpenDirectorySettingsKey)).toString();
  return !directory.isEmpty() && QDir(directory).exists() ? directory
                                                           : QString();
}

QString WorkspaceController::lastOpenFile() const {
  QSettings settings;
  const QString path =
      settings.value(QLatin1String(kLastOpenFileSettingsKey)).toString();
  const QFileInfo info(path);
  return !path.isEmpty() && info.exists() && info.isFile()
             ? info.absoluteFilePath()
             : QString();
}

void WorkspaceController::rememberOpenedFile(const QString& path) {
  const QFileInfo info(path);
  const QString normalized = info.absoluteFilePath();
  if (normalized.isEmpty()) {
    return;
  }

  QSettings settings;
  const QStringList stored =
      settings.value(QLatin1String(kRecentFilesSettingsKey)).toStringList();
  QStringList updated;
  updated.reserve(kMaxRecentFiles);
  updated.push_back(normalized);
  for (const QString& stored_path : stored) {
    if (updated.size() >= kMaxRecentFiles) {
      break;
    }
    if (stored_path.isEmpty()) {
      continue;
    }
    const QFileInfo stored_info(stored_path);
    const QString candidate = stored_info.absoluteFilePath();
    if (!stored_info.exists() || !stored_info.isFile() ||
        updated.contains(candidate)) {
      continue;
    }
    updated.push_back(candidate);
  }
  settings.setValue(QLatin1String(kRecentFilesSettingsKey), updated);
  settings.sync();
  rememberLastOpenDirectory(normalized);
  refreshRecentFilesMenu();
}

void WorkspaceController::updateNumericBaseButton() {
  if (w_.base_button == nullptr) {
    return;
  }
  const bool hexadecimal =
      view_state_.numeric_base == pnga::ui::qt::NumericBase::kHexadecimal;
  const QString target = hexadecimal ? QStringLiteral("DEC")
                                     : QStringLiteral("HEX");
  w_.base_button->setText(target);
  w_.base_button->setToolTip(QStringLiteral("Switch to %1").arg(target));
}
