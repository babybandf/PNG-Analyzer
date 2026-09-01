# MainWindow Behavior-Preserving Decomposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Split the 2,008-line MainWindow implementation into focused Qt application components without changing any existing user-visible, asynchronous, settings, file-picker, drag/drop, trace, or visual behavior.

**Architecture:** MainWindow stays the only public QMainWindow facade and Qt event receiver. It composes a non-owning MainWindowWidgets aggregate, a document session, and narrow workspace, selection-navigation, and trace controllers; the four existing QThread workers move into one dedicated translation unit. Each slice adds its destination to the executable and all MainWindow test targets before removing source from the facade.

**Tech Stack:** C++20, Qt 6 Widgets/Test, CMake, QSettings, QThread, pnga::analysis_engine, pnga::png_format, pnga::ui_qt.

**Spec:** docs/development/wp-5u15-main-window-decomposition.md

## Global Constraints

- This is a pure refactor: no Compression, Statistics, APNG, parser, decoder, public libs API, thread model, cache/budget policy, user-visible string, action name, shortcut, or settings-migration change.
- Do not modify libs, ui/qt, third_party, packaging, corpus files, existing settings keys, widget objectName values, or weaken/remove/retime tests.
- The Open filter remains exactly PNG files (*.png *.PNG); drag/drop remains the current local .png predicate. APNG suffix work is WP-705.
- Preserve trace policy exactly: 4,096 tokens, 8 MiB output budget, 64 MiB trace-index output limit, one worker, existing interval deduplication, and cancellation.
- Preserve widget creation order, QObject parentage, dock features/areas/default sizes, tab order, accessibility names, menu order, title, and native-theme behavior.
- Replacement and close increment document generation before clearing state. A stale worker or trace result is discarded before any widget/controller sees it.
- New .cpp files normally have no more than 500 lines. Final main_window.cpp has at most 600 lines and main_window.h at most 160.
- Each numbered task is one commit and leaves its named test cycle passing. Stop and report BLOCKED if preservation requires a public libs API change or an observable behavior change.

---

## File Structure

| Path | Change | Single responsibility |
|---|---|---|
| apps/png-analyzer-gui/src/document_workers.{h,cpp} | Create | Existing Decode/Stage/Validation/ChunkDetail QThread types and QueryStatusBridge. |
| apps/png-analyzer-gui/src/main_window_ui.{h,cpp} | Create | Widget/dock/menu/action construction and typed handles only. |
| apps/png-analyzer-gui/src/workspace_controller.{h,cpp} | Create | QSettings, layout defaults/restore/save, and recent-file behavior. |
| apps/png-analyzer-gui/src/document_session.{h,cpp} | Create | Source/index/results/generation and worker lifecycle/publication. |
| apps/png-analyzer-gui/src/selection_navigation_controller.{h,cpp} | Create | SelectionViewState, SelectionBus, coordinate lock, chunk/pixel/hex navigation. |
| apps/png-analyzer-gui/src/trace_controller.{h,cpp} | Create | TraceOrchestrator/state/binding, deduplication, cancellation, queued publication. |
| apps/png-analyzer-gui/src/main_window.{h,cpp} | Modify | Facade composition, presentation, high-level wiring, Qt event forwarding. |
| apps/png-analyzer-gui/CMakeLists.txt | Modify | One application source-list for the executable. |
| tests/gui/CMakeLists.txt | Modify | Source parity for three existing MainWindow test executables and focused tests. |
| tests/gui/{document_workers,main_window_ui,workspace_controller,document_session,selection_navigation_controller,trace_controller}_test.cpp | Create | Direct contracts for extracted units. |
| tests/gui/check_main_window_line_budget.cmake | Create | Enforces final facade line limits at build time. |

Define the application-source list in apps/png-analyzer-gui/CMakeLists.txt, immediately before the executable:

~~~cmake
set(PNGA_GUI_APP_SOURCES
  src/main_window.cpp
  src/document_workers.cpp
  src/main_window_ui.cpp
  src/workspace_controller.cpp
  src/document_session.cpp
  src/selection_navigation_controller.cpp
  src/trace_controller.cpp
)

add_executable(pnga_analyzer_gui
  src/main.cpp
  ${PNGA_GUI_APP_SOURCES}
  ${PNGA_GUI_PLATFORM_SOURCES}
)
~~~

Define this normalized test-source list once in tests/gui/CMakeLists.txt, and use it in pnga_gui_main_window_layout_tests, pnga_gui_trace_pipeline_integration_tests, and pnga_gui_cross_platform_gate_tests:

~~~cmake
set(PNGA_GUI_APP_TEST_SOURCES
  ../../apps/png-analyzer-gui/src/main_window.cpp
  ../../apps/png-analyzer-gui/src/document_workers.cpp
  ../../apps/png-analyzer-gui/src/main_window_ui.cpp
  ../../apps/png-analyzer-gui/src/workspace_controller.cpp
  ../../apps/png-analyzer-gui/src/document_session.cpp
  ../../apps/png-analyzer-gui/src/selection_navigation_controller.cpp
  ../../apps/png-analyzer-gui/src/trace_controller.cpp
)
~~~

Do not compile src/main.cpp into tests. Keep AUTOMOC ON for every target that compiles a Q_OBJECT header.

Add this focused-test helper once in tests/gui/CMakeLists.txt after the source list. Each later task invokes the helper with the exact target/test names shown in that task.

~~~cmake
function(add_pnga_gui_app_test target source test_name)
  add_executable(${target}
    ${source}
    ${PNGA_GUI_APP_TEST_SOURCES}
  )
  set_target_properties(${target} PROPERTIES AUTOMOC ON)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/png-analyzer-gui/src
  )
  target_link_libraries(${target} PRIVATE
    pnga::ui_qt
    pnga::analysis_engine
    pnga::png_format
    pnga::io
    pnga::core
    Qt6::Test
  )
  target_compile_features(${target} PRIVATE cxx_std_20)
  add_test(NAME ${test_name} COMMAND ${target})
endfunction()
~~~

## Cross-Task Interfaces

~~~cpp
// main_window_ui.h
struct MainWindowWidgets final {
  pnga::ui::qt::HexView* hex = nullptr;
  pnga::ui::qt::DeliveredImageView* image_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* pixel_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* filtered_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* defiltered_view = nullptr;
  pnga::ui::qt::SelectionBus* bus = nullptr;
  pnga::ui::qt::StageInspector* inspector = nullptr;
  pnga::ui::qt::BlockInspector* block_inspector = nullptr;
  pnga::ui::qt::HuffmanInspector* huffman_inspector = nullptr;
  pnga::ui::qt::DecodeTraceInspector* decode_trace_inspector = nullptr;
  pnga::ui::qt::TraceInspectorBinding* trace_binding = nullptr;
  pnga::ui::qt::CompressionContext* compression_context = nullptr;
  QDockWidget* chunks_dock = nullptr;
  QDockWidget* inspector_dock = nullptr;
  QSplitter* chunks_splitter = nullptr;
  pnga::ui::qt::ChunkDetailPanel* chunk_detail = nullptr;
  QTabWidget* preview_tabs = nullptr;
  QTabWidget* inspector_tabs = nullptr;
  QTabWidget* compression_inspector_tabs = nullptr;
  QWidget* hex_panel = nullptr;
  pnga::ui::qt::HexSourceTabBar* hex_source_tabs = nullptr;
  QSplitter* center_splitter = nullptr;
  QSpinBox* x_spin = nullptr;
  QSpinBox* y_spin = nullptr;
  QCheckBox* lock_check = nullptr;
  QPushButton* base_button = nullptr;
  QTreeView* tree = nullptr;
  QLabel* pixel_label = nullptr;
  QLabel* validation_label = nullptr;
  QAction* open_action = nullptr;
  QAction* close_action = nullptr;
  QAction* exit_action = nullptr;
  QAction* reset_layout_action = nullptr;
  QAction* show_hex_view_action = nullptr;
  QAction* show_chunks_action = nullptr;
  QAction* show_inspector_action = nullptr;
  QMenu* recent_files_menu = nullptr;
  QAction* theme_system_action = nullptr;
  QAction* theme_light_action = nullptr;
  QAction* theme_dark_action = nullptr;
};
MainWindowWidgets buildMainWindowUi(
    QMainWindow& window, pnga::ui::qt::ApplicationTheme* theme);
~~~

~~~cpp
// document_session.h
class DocumentSession final : public QObject {
  Q_OBJECT
 public:
  explicit DocumentSession(QObject* parent = nullptr);
  bool replace(const QString& path);
  void close();
  void startPrimaryWorkers();
  void openQueryCoordinator(
      const pnga::png_reconstruction::ImageHeader& header);
  void requestChunkDetail(const pnga::png_format::ChunkNode& node,
                          std::uint64_t selection_serial);
  std::uint64_t generation() const noexcept;
  bool hasDocument() const noexcept;
  const QString& currentFilePath() const noexcept;
  const pnga::png_format::ChunkIndex& index() const noexcept;
  std::shared_ptr<const pnga::io::IByteSource> source() const;
  const pnga::backend_libpng::ReferenceResult& decodeResult() const noexcept;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stageSet() const;
  const pnga::analysis_engine::DocumentValidationReport& validationReport()
      const noexcept;
  const pnga::png_format::ChunkDetail& chunkDetail() const noexcept;
  pnga::analysis_engine::QueryCoordinator* queryCoordinator() noexcept;
 signals:
  void replaced(std::uint64_t generation);
  void closed(std::uint64_t generation);
  void decodePublished(std::uint64_t generation);
  void stagesPublished(std::uint64_t generation);
  void validationPublished(std::uint64_t generation);
  void chunkDetailPublished(std::uint64_t generation,
                            std::uint64_t selection_serial);
  void rowQueryStatus(std::uint64_t row, int status);
};
~~~

~~~cpp
// workspace_controller.h
class WorkspaceController final {
 public:
  using OpenRecentFile = std::function<void(const QString&)>;
  WorkspaceController(QMainWindow& window, MainWindowWidgets widgets,
                      OpenRecentFile open_recent_file);
  void restore();
  void save() const;
  void applyDefaults();
  void configureDockInteraction();
  void refreshRecentFilesMenu();
  void rememberOpenedFile(const QString& path);
  void rememberLastOpenDirectory(const QString& path);
  QString lastOpenDirectory() const;
  QString lastOpenFile() const;
};
~~~

~~~cpp
// selection_navigation_controller.h
struct SelectionNavigationCallbacks final {
  std::function<void(const pnga::trace_model::ImageCoordinate&)> request_trace;
  std::function<void(std::uint64_t)> request_scanline;
};
class SelectionNavigationController final : public QObject {
  Q_OBJECT
 public:
  SelectionNavigationController(MainWindowWidgets widgets,
                                SelectionNavigationCallbacks callbacks,
                                QObject* parent = nullptr);
  void setDocument(std::uint64_t generation,
                   std::shared_ptr<const pnga::io::IByteSource> source,
                   const pnga::png_format::ChunkIndex* index,
                   pnga::analysis_engine::QueryCoordinator* query);
  void clearDocument(std::uint64_t generation);
  void replaceChunkModel(const pnga::png_format::ChunkIndex* index);
  void setDefaultPixelStatus(const QString& text);
  void setQueryCoordinator(pnga::analysis_engine::QueryCoordinator* query);
  void onStageSetPublished(
      const std::shared_ptr<const pnga::analysis_engine::StageSet>& stages);
  pnga::ui::qt::SelectionViewState& viewState() noexcept;
  const pnga::ui::qt::SelectionViewState& viewState() const noexcept;
 public slots:
  void onChunkSelectionChanged(const QModelIndex& current,
                               const QModelIndex& previous);
  void onPixelSelected(int x, int y);
  void onPixelHovered(int x, int y);
  void onPixelHoverLeft();
  void publishLockedCoordinate();
  void clearLockedCoordinate();
  void nudgeLockedCoordinate(int dx, int dy);
  void setHexSource(pnga::ui::qt::HexSource source);
};
~~~

~~~cpp
// trace_controller.h
class TraceController final : public QObject {
  Q_OBJECT
 public:
  explicit TraceController(MainWindowWidgets widgets, QObject* parent = nullptr);
  void replaceDocument(std::uint64_t generation,
                       std::shared_ptr<const pnga::io::IByteSource> source);
  void clearDocument(std::uint64_t generation);
  void setQueryCoordinator(pnga::analysis_engine::QueryCoordinator* query);
  void requestFor(const pnga::trace_model::ImageCoordinate& coordinate);
  void setSelectedOutputOffset(std::optional<std::uint64_t> output_offset);
  void setSelectedScanline(std::optional<std::uint64_t> scanline);
  std::uint64_t generation() const noexcept;
 signals:
  void hexSourceRequested(pnga::ui::qt::HexSource source);
  void hexRangeRequested(std::uint64_t begin, std::uint64_t end);
};
~~~

### Task 1: Characterize facade behavior and establish source-list parity

**Files:**
- Modify: apps/png-analyzer-gui/CMakeLists.txt
- Modify: tests/gui/CMakeLists.txt
- Modify: tests/gui/main_window_layout_test.cpp
- Modify: tests/gui/trace_pipeline_integration_test.cpp

**Interfaces:**
- Consumes: the existing monolithic MainWindow.
- Produces: source-list variables and tests that later extractions must preserve.

- [ ] **Step 1: Add behavioral characterization tests**

Add these slots and bodies to MainWindowLayoutTest:

~~~cpp
void facadeKeepsStableActionAndStatusIdentities();
void replacingOpenDocumentResetsVisiblePrimaryState();

void MainWindowLayoutTest::facadeKeepsStableActionAndStatusIdentities() {
  MainWindow window;
  QCOMPARE(window.windowTitle(), QStringLiteral("PNG Analyzer"));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("closeImageAction")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("exitAction")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showHexView")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showChunkList")));
  QVERIFY(window.findChild<QAction*>(QStringLiteral("showInspector")));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("pixelStatus"))->text(),
           QStringLiteral("No image"));
  QCOMPARE(window.findChild<QLabel*>(QStringLiteral("validationStatus"))->text(),
           QStringLiteral("Validation: not loaded"));
}
~~~

Implement replacingOpenDocumentResetsVisiblePrimaryState with two QTemporaryFile instances containing the existing 1×1 base64 fixture. Open the first, set previewTabs and inspectorTabs to 1, open the second, then assert both indices are 0, the close action is enabled, and the title has the second absolute-path suffix.

Add replacingDocumentCannotPublishTheFirstTraceBundle to TracePipelineIntegrationTest. It opens a valid first fixture, waits for compressionContextStatus to contain ready, opens a second fixture, waits again for ready, and asserts BlockInspector::view().generation is non-zero. Do not assert a fixed generation: the contract is visible state coherent with the latest document.

- [ ] **Step 2: Run characterization on the untouched monolith**

Run:

~~~bash
cmake --build --preset dev --target pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_main_window_layout_tests|gui_trace_pipeline_integration_tests)$' --output-on-failure
~~~

Expected: both tests pass before production extraction.

- [ ] **Step 3: Introduce CMake source variables**

Initially set PNGA_GUI_APP_SOURCES to only src/main_window.cpp, and PNGA_GUI_APP_TEST_SOURCES to only its normalized test path. Replace the literal source in the executable and the three existing MainWindow targets with these variables. Do not alter libraries, test names, CTest properties, or compile features.

- [ ] **Step 4: Reconfigure and verify neutrality**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_analyzer_gui pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_cross_platform_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_main_window_layout_tests|gui_trace_pipeline_integration_tests|gui_cross_platform_gate_tests)$' --output-on-failure
~~~

Expected: every named test passes.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/main_window_layout_test.cpp tests/gui/trace_pipeline_integration_test.cpp
git commit -m "test: characterize main window refactor behavior"
~~~

### Task 2: Extract QThread workers and QueryStatusBridge

**Files:**
- Create: apps/png-analyzer-gui/src/document_workers.h
- Create: apps/png-analyzer-gui/src/document_workers.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: apps/png-analyzer-gui/CMakeLists.txt
- Modify: tests/gui/CMakeLists.txt
- Create: tests/gui/document_workers_test.cpp

**Interfaces:**
- Consumes: current worker constructors, result getters, and signals.
- Produces: DecodeWorker, StageWorker, ValidationWorker, ChunkDetailWorker, and QueryStatusBridge from document_workers.h with identical signatures.

- [ ] **Step 1: Write the compile-first worker identity test**

Create tests/gui/document_workers_test.cpp:

~~~cpp
#include "document_workers.h"
#include <QtTest/QtTest>

class DocumentWorkersTest : public QObject {
  Q_OBJECT
 private slots:
  void workersPreserveRequestIdentity();
};

void DocumentWorkersTest::workersPreserveRequestIdentity() {
  DecodeWorker decode(17, nullptr);
  StageWorker stages(18, nullptr);
  pnga::png_format::ChunkIndex index;
  ValidationWorker validation(19, nullptr, index);
  pnga::png_format::ChunkNode node;
  ChunkDetailWorker detail(20, 21, nullptr, node);
  QCOMPARE(decode.generation(), std::uint64_t{17});
  QCOMPARE(stages.generation(), std::uint64_t{18});
  QCOMPARE(validation.generation(), std::uint64_t{19});
  QCOMPARE(detail.generation(), std::uint64_t{20});
  QCOMPARE(detail.selectionSerial(), std::uint64_t{21});
}

QTEST_MAIN(DocumentWorkersTest)
#include "document_workers_test.moc"
~~~

Register it with:

~~~cmake
add_pnga_gui_app_test(pnga_gui_document_workers_tests
  document_workers_test.cpp gui_document_workers_tests)
~~~

- [ ] **Step 2: Run before the header exists**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_document_workers_tests --parallel 4
~~~

Expected: compilation fails because document_workers.h does not exist.

- [ ] **Step 3: Move declarations and bodies verbatim**

Move the five type declarations from main_window.h to document_workers.h, retaining every constructor, getter, signal, member type, and Q_OBJECT. Move the four constructors and run bodies to document_workers.cpp. The four work calls remain exactly:

~~~cpp
result_ = pnga::backend_libpng::decode_reference(*source_);
result_ = std::make_shared<pnga::analysis_engine::StageSet>(
    pnga::analysis_engine::analyze_source(*source_));
result_ = pnga::analysis_engine::validate_document(*source_, index_);
result_ = pnga::png_format::describe_chunk(*source_, node_);
~~~

Include document_workers.h from main_window.h. Do not change worker parents, connection types, deleteLater, result copying, worker creation, or stale-result checks. Append the new .cpp to both shared source lists.

- [ ] **Step 4: Run focused and regression tests**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_document_workers_tests pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_document_workers_tests|gui_main_window_layout_tests|gui_trace_pipeline_integration_tests)$' --output-on-failure
~~~

Expected: all named tests pass.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/document_workers.h apps/png-analyzer-gui/src/document_workers.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/document_workers_test.cpp
git commit -m "refactor: extract document workers"
~~~

### Task 3: Extract deterministic UI construction

**Files:**
- Create: apps/png-analyzer-gui/src/main_window_ui.h
- Create: apps/png-analyzer-gui/src/main_window_ui.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: both CMake files
- Create: tests/gui/main_window_ui_test.cpp

**Interfaces:**
- Consumes: MainWindowWidgets and buildMainWindowUi from Cross-Task Interfaces.
- Produces: the same fully-parented widget graph; the builder owns no document, workspace, selection, or trace behavior.

- [ ] **Step 1: Write the direct builder test**

Create tests/gui/main_window_ui_test.cpp:

~~~cpp
#include "main_window_ui.h"
#include <QtTest/QtTest>

class MainWindowUiTest : public QObject {
  Q_OBJECT
 private slots:
  void builderCreatesStableWidgetAndActionIdentities();
};

void MainWindowUiTest::builderCreatesStableWidgetAndActionIdentities() {
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  QCOMPARE(window.centralWidget(), widgets.center_splitter);
  QCOMPARE(widgets.preview_tabs->objectName(), QStringLiteral("previewTabs"));
  QCOMPARE(widgets.preview_tabs->count(), 4);
  QCOMPARE(widgets.chunks_dock->objectName(), QStringLiteral("chunksDock"));
  QCOMPARE(widgets.inspector_dock->objectName(), QStringLiteral("inspectorDock"));
  QCOMPARE(widgets.close_action->objectName(), QStringLiteral("closeImageAction"));
  QVERIFY(!widgets.close_action->isEnabled());
  QCOMPARE(widgets.pixel_label->objectName(), QStringLiteral("pixelStatus"));
  QCOMPARE(widgets.validation_label->objectName(), QStringLiteral("validationStatus"));
}

QTEST_MAIN(MainWindowUiTest)
#include "main_window_ui_test.moc"
~~~

Register it with:

~~~cmake
add_pnga_gui_app_test(pnga_gui_main_window_ui_tests
  main_window_ui_test.cpp gui_main_window_ui_tests)
~~~

- [ ] **Step 2: Run before implementation**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_main_window_ui_tests --parallel 4
~~~

Expected: compilation fails because main_window_ui.h does not exist.

- [ ] **Step 3: Implement builder-only construction**

Move, in current order, every allocation/configuration of splitter, preview pages, hex panel, docks, tree, ChunkDetailPanel, SelectionBus, inspector pages, coordinate controls, Compression widgets, status labels, menus, actions, and theme menu into buildMainWindowUi.

Retain every existing parent, objectName, accessible name, shortcut, dock feature/area, minimum size, stretch factor, initial splitter size, tab order, menu/tab/status text, and action state. Return every object used outside builder in MainWindowWidgets.

The builder does not connect to MainWindow slots, capture MainWindow, map files, start workers, read/write settings, submit trace work, or restore workspace. It creates TraceInspectorBinding from the three inspector widgets and returns it as trace_binding. It does not create a ChunkModel because the model depends on the current document index; until Task 6, MainWindow keeps the existing empty-model creation immediately after building widgets_. Append the source to both source lists.

- [ ] **Step 4: Verify builder and layout**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_main_window_ui_tests pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_cross_platform_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_main_window_ui_tests|gui_main_window_layout_tests|gui_trace_pipeline_integration_tests|gui_cross_platform_gate_tests)$' --output-on-failure
~~~

Expected: all named tests pass. Repair construction order in production code, not tests.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/main_window_ui.h apps/png-analyzer-gui/src/main_window_ui.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/main_window_ui_test.cpp
git commit -m "refactor: extract main window ui construction"
~~~

### Task 4: Extract workspace, settings, and recent-file behavior

**Files:**
- Create: apps/png-analyzer-gui/src/workspace_controller.h
- Create: apps/png-analyzer-gui/src/workspace_controller.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: both CMake files
- Create: tests/gui/workspace_controller_test.cpp

**Interfaces:**
- Consumes: WorkspaceController declaration and MainWindowWidgets.
- Produces: all workspace behavior without arbitrary child lookup or direct QSettings code in MainWindow.

- [ ] **Step 1: Write the direct workspace contract test**

Create WorkspaceControllerTest with this body:

~~~cpp
void WorkspaceControllerTest::defaultsAndRecentFilesKeepExistingKeys() {
  QSettings settings;
  settings.clear();
  QMainWindow window;
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
    workspace.rememberOpenedFile(
        dir.filePath(QStringLiteral("i%1.png").arg(i)));
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
~~~

Add standard QTest scaffolding and register it with:

~~~cmake
add_pnga_gui_app_test(pnga_gui_workspace_controller_tests
  workspace_controller_test.cpp gui_workspace_controller_tests)
~~~

- [ ] **Step 2: Run before implementation**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_workspace_controller_tests --parallel 4
~~~

Expected: compilation fails because workspace_controller.h does not exist.

- [ ] **Step 3: Move exact workspace behavior**

Move applyDefaultWorkspace, configureDockInteraction, restoreWorkspace, saveWorkspace, refreshRecentFilesMenu, rememberOpenedFile, rememberLastOpenDirectory, lastOpenDirectory, lastOpenFile, openRecentFile, and workspace-specific resetLayout work.

Keep QSettings organization/application names, every key/default/migration/version branch, recent cap, 1200×760 default, 840×520 minimum, Preview/Hex ratio, dock values, and reset lock/hover preservation unchanged. The former openRecentFile invokes the stored callback only; MainWindow's one-line facade keeps the unreadable-file warning. MainWindow constructs:

~~~cpp
workspace_ = std::make_unique<WorkspaceController>(
    *this, widgets_, [this](const QString& path) { openRecentFile(path); });
~~~

closeEvent calls workspace_->save(). onOpenTriggered uses directory/file accessors and retains the exact PNG-only dialog setup.

- [ ] **Step 4: Verify workspace regression coverage**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_workspace_controller_tests pnga_gui_main_window_layout_tests pnga_gui_cross_platform_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_workspace_controller_tests|gui_main_window_layout_tests|gui_cross_platform_gate_tests)$' --output-on-failure
~~~

Expected: corrupt-settings fallback, migration, recent cap, and Reset Layout tests pass unchanged.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/workspace_controller.h apps/png-analyzer-gui/src/workspace_controller.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/workspace_controller_test.cpp
git commit -m "refactor: extract workspace controller"
~~~

### Task 5: Extract document session and generation-gated publication

**Files:**
- Create: apps/png-analyzer-gui/src/document_session.h
- Create: apps/png-analyzer-gui/src/document_session.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: both CMake files
- Create: tests/gui/document_session_test.cpp

**Interfaces:**
- Consumes: DocumentSession declaration and workers from Task 2.
- Produces: session ownership of source/index/results/query/workers/generation and generation-gated signals.

- [ ] **Step 1: Write stale-publication tests**

Create DocumentSessionTest using the existing 1×1 base64 fixture:

~~~cpp
void DocumentSessionTest::closeInvalidatesPendingWorkerPublication() {
  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();

  DocumentSession session;
  QSignalSpy decoded(&session, &DocumentSession::decodePublished);
  QVERIFY(session.replace(png.fileName()));
  const std::uint64_t opened = session.generation();
  session.startPrimaryWorkers();
  session.close();
  QCOMPARE(session.generation(), opened + 1);
  QVERIFY(!session.hasDocument());
  QTest::qWait(200);
  QCOMPARE(decoded.count(), 0);
}
~~~

Add replacePublishesOnlyCurrentGeneration: replace first valid temporary PNG, start workers, immediately replace second, start workers, wait for decodePublished, and compare the signal argument to session.generation().

Register it with:

~~~cmake
add_pnga_gui_app_test(pnga_gui_document_session_tests
  document_session_test.cpp gui_document_session_tests)
~~~

- [ ] **Step 2: Run before implementation**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_document_session_tests --parallel 4
~~~

Expected: compilation fails because document_session.h does not exist.

- [ ] **Step 3: Implement generation-first session lifecycle**

Move source, index, immutable stage set, validation report, detail result, QueryCoordinator, QueryStatusBridge, four worker pointers, generation, and current path to the session. replace(path) maps with pnga::io::open_mapped_file(filesystemPath(path), opened), returns false without changing state on error, takes shared ownership on success, increments generation, indexes chunks, clears prior results/query/workers, emits replaced(generation), and returns true. close() increments first, clears state, and emits closed(generation).

startPrimaryWorkers starts Decode/Stage/Validation with current source/index/generation. Private receiving slots retain worker parent, finished -> deleteLater, generation comparison, and publication ordering; they store and emit only for current worker/current generation. In the current-generation Stage receiving slot, create QueryCoordinator with worker_count 2 and replay_budget_bytes 1ull << 26, open it with the current shared source, stage header, and anchor_interval_bytes 16384, and retain the present queued QueryStatusBridge callback. The bridge forwards through DocumentSession::rowQueryStatus. requestChunkDetail performs the same generation-and-selection-serial gate.

MainWindow keeps title suffix, close action, labels, UI clear, primary-tab reset, SelectionBus generation update, and starts workers only after visual reset. It uses session accessors rather than owning source/index/query/results/workers. On stagesPublished it first gives selection_ and trace_ the non-null session_->queryCoordinator(), then lets trace_ open for the current source/generation.

- [ ] **Step 4: Verify session and UI behavior**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_document_session_tests pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_document_session_tests|gui_main_window_layout_tests|gui_trace_pipeline_integration_tests)$' --output-on-failure
~~~

Expected: all named tests pass; stale results are silently dropped.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/document_session.h apps/png-analyzer-gui/src/document_session.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/document_session_test.cpp
git commit -m "refactor: extract document session"
~~~

### Task 6: Extract selection, coordinate, chunk, and Hex navigation

**Files:**
- Create: apps/png-analyzer-gui/src/selection_navigation_controller.h
- Create: apps/png-analyzer-gui/src/selection_navigation_controller.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: both CMake files
- Create: tests/gui/selection_navigation_controller_test.cpp

**Interfaces:**
- Consumes: selection controller, widgets, DocumentSession accessors, and QueryCoordinator pointer.
- Produces: one owner for SelectionViewState, SelectionBus, coordinate controls, chunk detail, and hex navigation.

- [ ] **Step 1: Write no-hover-replay and coordinate-commit tests**

Create a test with built MainWindowWidgets and callbacks recording trace requests:

~~~cpp
void SelectionNavigationControllerTest::pixelCommitPublishesLockAndRequestsTrace() {
  QMainWindow window;
  MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  int trace_requests = 0;
  std::optional<pnga::trace_model::ImageCoordinate> coordinate;
  SelectionNavigationController controller(
      widgets,
      { [&trace_requests, &coordinate](const auto& value) {
          ++trace_requests;
          coordinate = value;
        }, [](std::uint64_t) {} });
  controller.setDocument(9, nullptr, nullptr, nullptr);
  controller.onPixelSelected(3, 4);
  QVERIFY(widgets.lock_check->isChecked());
  QCOMPARE(widgets.x_spin->value(), 3);
  QCOMPARE(widgets.y_spin->value(), 4);
  QCOMPARE(trace_requests, 1);
  QVERIFY(coordinate.has_value());
  QCOMPARE(coordinate->x, std::uint64_t{3});
  QCOMPARE(coordinate->y, std::uint64_t{4});
}
~~~

Add hoverDoesNotRequestTrace: call onPixelHovered(7, 8), onPixelHoverLeft(), assert request count is zero and viewState().hover is empty. Add hexSourceSelectionUpdatesViewState: call setHexSource(HexSource::kInflated) and compare viewState().hex_source.

Register it with:

~~~cmake
add_pnga_gui_app_test(pnga_gui_selection_navigation_controller_tests
  selection_navigation_controller_test.cpp
  gui_selection_navigation_controller_tests)
~~~

- [ ] **Step 2: Run before implementation**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_selection_navigation_controller_tests --parallel 4
~~~

Expected: compilation fails because the controller header does not exist.

- [ ] **Step 3: Move exact navigation code**

Move model-replacement portion of resetDocument, onChunkSelectionChanged, applyChunkHexHighlight, chunk-detail presentation, setPixelStatus, restorePixelStatus, onPixelSelected, publishLockedCoordinate, clearLockedCoordinate, nudgeLockedCoordinate, updateNumericBaseButton, updateHexSource, setHexSource, and event-filter behavior for X/Y/lock/base/preview/hex/inspector controls.

Move their file-local constants/helpers into this .cpp: kChunkPanelOrigin == 1, kImagePanelOrigin == 2, kHexPanelOrigin == 3, kHeaderSpanLength, kCrcSpanLength, filtered_output_offset_for_pixel, and navigation-only Hex helpers. Preserve body semantics and highlight colors.

replaceChunkModel deletes old model, creates ChunkModel(index), installs it, reconnects currentChanged, refreshes selected Hex source, and selects row zero with existing flags if rows exist. The controller maps no file, owns no worker, and creates no trace orchestrator. Its request_trace callback calls trace_controller_->requestFor(coordinate); its request_scanline callback retains selection-priority query and Inspector status update.

- [ ] **Step 4: Verify navigation and DPI behavior**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_selection_navigation_controller_tests pnga_gui_main_window_layout_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_cross_platform_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_selection_navigation_controller_tests|gui_main_window_layout_tests|gui_trace_pipeline_integration_tests|gui_cross_platform_gate_tests|gui_cross_platform_gate_dpi_150_tests|gui_cross_platform_gate_dpi_200_tests)$' --output-on-failure
~~~

Expected: all named tests pass with unchanged chunk/detail/Hex behavior.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/selection_navigation_controller.h apps/png-analyzer-gui/src/selection_navigation_controller.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/selection_navigation_controller_test.cpp
git commit -m "refactor: extract selection navigation"
~~~

### Task 7: Extract bounded trace orchestration

**Files:**
- Create: apps/png-analyzer-gui/src/trace_controller.h
- Create: apps/png-analyzer-gui/src/trace_controller.cpp
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: both CMake files
- Create: tests/gui/trace_controller_test.cpp

**Interfaces:**
- Consumes: TraceController, session generation/source, query coordinator, and SelectionNavigationController callback.
- Produces: sole ownership of trace objects, request interval state, queued callback, binding/state-machine publication.

- [ ] **Step 1: Write deduplication and generation tests**

Under #ifdef PNGA_TRACE_CONTROLLER_TESTING, declare in TraceController:

~~~cpp
std::size_t acceptedRequestCountForTest() const noexcept;
std::size_t cancelledRequestCountForTest() const noexcept;
~~~

Counters increment at existing submit/cancel decisions only. In trace_controller_test.cpp define these exact fixture helpers above the test class; they make source and query setup reproducible without using MainWindow:

~~~cpp
static std::shared_ptr<pnga::io::IByteSource> mappedTraceFixture(
    QTemporaryFile& png) {
  if (!png.open()) return {};
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  if (png.write(bytes) != bytes.size()) return {};
  png.flush();
  std::unique_ptr<pnga::io::IByteSource> opened;
  if (pnga::io::open_mapped_file(
          std::filesystem::path(png.fileName().toStdString()), opened)) return {};
  return std::shared_ptr<pnga::io::IByteSource>(opened.release());
}

static std::unique_ptr<pnga::analysis_engine::QueryCoordinator> readyQuery(
    const std::shared_ptr<pnga::io::IByteSource>& source) {
  const auto stages = pnga::analysis_engine::analyze_source(*source);
  auto query = std::make_unique<pnga::analysis_engine::QueryCoordinator>(
      2, 1ull << 26);
  const std::shared_ptr<const pnga::io::IByteSource> shared(source);
  if (!query->open(shared, stages.header, 16384)) return {};
  return query;
}
~~~

~~~cpp
void TraceControllerTest::identicalCommittedIntervalIsSubmittedOnce() {
  QTemporaryFile png;
  const auto source = mappedTraceFixture(png);
  QVERIFY(source);
  const auto query = readyQuery(source);
  QVERIFY(query);
  QMainWindow window;
  const MainWindowWidgets widgets = buildMainWindowUi(window, nullptr);
  TraceController controller(widgets);
  controller.setQueryCoordinator(query.get());
  controller.replaceDocument(5, source);
  controller.requestFor({0, 0, 0, 0, 0});
  QTRY_COMPARE_WITH_TIMEOUT(
      controller.acceptedRequestCountForTest(), std::size_t{1}, 5000);
  controller.requestFor({0, 0, 0, 0, 0});
  QCOMPARE(controller.acceptedRequestCountForTest(), std::size_t{1});
}
~~~

Add replacementDropsOldGenerationResult with the same helper calls, then use this body after controller construction:

~~~cpp
controller.setQueryCoordinator(query.get());
controller.replaceDocument(5, source);
controller.requestFor({0, 0, 0, 0, 0});
controller.replaceDocument(6, source);
controller.setQueryCoordinator(query.get());
controller.requestFor({0, 0, 0, 0, 0});
QTRY_COMPARE_WITH_TIMEOUT(widgets.block_inspector->view().generation,
                          std::uint64_t{6}, 5000);
QCOMPARE(widgets.huffman_inspector->view().generation, std::uint64_t{6});
QCOMPARE(widgets.decode_trace_inspector->view().generation, std::uint64_t{6});
~~~

Compile only this target with PNGA_TRACE_CONTROLLER_TESTING=1.

Register and scope the macro as follows:

~~~cmake
add_pnga_gui_app_test(pnga_gui_trace_controller_tests
  trace_controller_test.cpp gui_trace_controller_tests)
target_compile_definitions(pnga_gui_trace_controller_tests PRIVATE
  PNGA_TRACE_CONTROLLER_TESTING=1)
~~~

- [ ] **Step 2: Run before implementation**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_trace_controller_tests --parallel 4
~~~

Expected: compilation fails because trace_controller.h does not exist.

- [ ] **Step 3: Transfer trace state and policy exactly**

Move trace orchestrator/state machine/binding/task handle/cached result/pending coordinate/scanline/output-offset/interval/request-generation/deflate-data-begin fields and bodies of openTraceCoordinator, onTraceResult, and requestTraceFor. Move trace-only constants/helpers: 4,096 token budget, 8 MiB output, 64 MiB index output, byte_range_for_bits, and checked arithmetic.

replaceDocument clears old objects, assigns generation, calls TraceInspectorStateMachine::replaceDocument(generation), clears binding, sets setHasDocument(true), then creates exactly:

~~~cpp
auto trace = std::make_unique<pnga::analysis_engine::TraceOrchestrator>(
    /*worker_count=*/1,
    /*max_reserved_bytes=*/kTraceOutputBudgetBytes * 2);
if (!trace->open(source, kTraceIndexOutputBytes)) {
  return;
}
trace->setDocumentGeneration(generation);
~~~

The result callback remains QMetaObject::invokeMethod with Qt::QueuedConnection onto the controller. clearDocument cancels an accepted handle, resets interval/result state, replaces state generation, clears binding, and sets has-document false. requestFor retains pending-coordinate, row lookup, overflow/empty checks, same-generation same-interval reuse, selected output update, cancellation, and request construction.

Wire hexSourceRequested to selection_->setHexSource. Keep existing Show in Hex/Show in DEFLATE colors and offsets. Page switching, hover, resize, and numeric-base handling do not call requestFor.

- [ ] **Step 4: Verify trace and responsiveness**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_trace_controller_tests pnga_gui_trace_pipeline_integration_tests pnga_gui_compression_inspector_responsive_tests pnga_gui_cross_platform_gate_tests --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R '^(gui_trace_controller_tests|gui_trace_pipeline_integration_tests|gui_compression_inspector_responsive_tests|gui_cross_platform_gate_tests)$' --output-on-failure
~~~

Expected: all named tests pass; repeated committed interval causes one accepted request; stale generations do not publish.

- [ ] **Step 5: Commit**

~~~bash
git add apps/png-analyzer-gui/src/trace_controller.h apps/png-analyzer-gui/src/trace_controller.cpp apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/CMakeLists.txt tests/gui/CMakeLists.txt tests/gui/trace_controller_test.cpp
git commit -m "refactor: extract trace controller"
~~~

### Task 8: Finish the facade and execute the full quality gate

**Files:**
- Create: tests/gui/check_main_window_line_budget.cmake
- Modify: apps/png-analyzer-gui/src/main_window.h
- Modify: apps/png-analyzer-gui/src/main_window.cpp
- Modify: tests/gui/CMakeLists.txt
- Modify: docs/development/wp-5u15-main-window-decomposition.md

**Interfaces:**
- Consumes: every component above.
- Produces: thin facade and mechanized 600/160 line limits.

- [ ] **Step 1: Add a failing build-time line-budget gate**

Create tests/gui/check_main_window_line_budget.cmake:

~~~cmake
foreach(file IN ITEMS "${MAIN_WINDOW_CPP}" "${MAIN_WINDOW_H}")
  file(STRINGS "${file}" lines)
  list(LENGTH lines count)
  if(file STREQUAL "${MAIN_WINDOW_CPP}" AND count GREATER 600)
    message(FATAL_ERROR "main_window.cpp has ${count} lines; maximum is 600")
  endif()
  if(file STREQUAL "${MAIN_WINDOW_H}" AND count GREATER 160)
    message(FATAL_ERROR "main_window.h has ${count} lines; maximum is 160")
  endif()
endforeach()
~~~

Attach it after pnga_gui_main_window_layout_tests builds:

~~~cmake
add_custom_command(TARGET pnga_gui_main_window_layout_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DMAIN_WINDOW_CPP=${CMAKE_SOURCE_DIR}/apps/png-analyzer-gui/src/main_window.cpp
    -DMAIN_WINDOW_H=${CMAKE_SOURCE_DIR}/apps/png-analyzer-gui/src/main_window.h
    -P ${CMAKE_SOURCE_DIR}/tests/gui/check_main_window_line_budget.cmake
  VERBATIM
)
~~~

- [ ] **Step 2: Prove the current facade fails the gate**

Run:

~~~bash
cmake --preset dev
cmake --build --preset dev --target pnga_gui_main_window_layout_tests --parallel 4
~~~

Expected: failure reports a source file above its maximum.

- [ ] **Step 3: Delete duplicate facade state and complete wiring**

MainWindow retains only QMainWindow construction/destruction; openFile; open/close dialog presentation; title/close-action presentation; component creation/wiring; paintEvent, closeEvent, drag events, eventFilter; and short forwarding slots.

Its owned state is exactly:

~~~cpp
MainWindowWidgets widgets_;
std::unique_ptr<WorkspaceController> workspace_;
std::unique_ptr<DocumentSession> session_;
std::unique_ptr<SelectionNavigationController> selection_;
std::unique_ptr<TraceController> trace_;
~~~

Delete duplicate fields/methods/helpers/includes moved elsewhere. Preserve no-change-on-map-error: after successful session_->replace(path), retain recent-file update, title suffix replacement, labels, close action, UI reset, primary tabs, validation checking state, and session_->startPrimaryWorkers() order. Do not inspect decode/stage/trace internals.

Keep drag/drop equivalent to hasLocalPngUrl, paintEvent separator behavior, and every eventFilter return value.

- [ ] **Step 4: Run complete verification**

Run:

~~~bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_package_smoke.py --preset release --jobs 2
wc -l apps/png-analyzer-gui/src/main_window.cpp apps/png-analyzer-gui/src/main_window.h
git diff --check
git status --short
~~~

Expected: repository scripts report no failure/warning; configure/build, all CTest cases, GUI gate, and package smoke pass; main_window.cpp <= 600; main_window.h <= 160; and git diff --check is clean.

- [ ] **Step 5: Record verification and commit**

Only after Step 4 passes, change WP-5U15 status to implemented and verified with the actual date. Add a Verification record containing exact commands, test count, and final two line counts.

~~~bash
git add apps/png-analyzer-gui/src/main_window.h apps/png-analyzer-gui/src/main_window.cpp tests/gui/CMakeLists.txt tests/gui/check_main_window_line_budget.cmake docs/development/wp-5u15-main-window-decomposition.md
git commit -m "refactor: decompose main window facade"
~~~

## Plan Self-Review

| WP-5U15 requirement | Task coverage |
|---|---|
| Seven focused responsibilities; no catch-all abstraction | File Structure; Tasks 2–8 |
| Stable UI/layout/actions/menus/accessibility/theme behavior | Tasks 1 and 3 |
| QSettings migration, recent files, layout persistence/reset | Task 4 |
| Document source/index/results/workers and stale generation suppression | Task 5 |
| X/Y lock, origins, chunk/pixel/hex behavior, no hover replay | Task 6 |
| Bounded trace, queued callback, dedup/cancel, no non-commit replay | Tasks 1 and 7 |
| Executable and test source parity | File Structure and Task 1 |
| 600/160 line gates and full verification | Task 8 |
| No APNG or picker-filter scope creep | Global Constraints; Tasks 3–8 |

Cross-task method names are consistent: replace, close, startPrimaryWorkers, requestChunkDetail, requestFor, and setHexSource. The plan introduces only the approved focused files and no helper/common/misc/manager/utils catch-all.
