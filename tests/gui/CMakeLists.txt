# Qt tests (layout contract §11: tests/gui). Built only when Qt is present
# (the GUI target is optional; CI runners do not install Qt).

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(pnga_gui_chunk_model_tests
  chunk_model_test.cpp
)
set_target_properties(pnga_gui_chunk_model_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_chunk_model_tests PRIVATE
  pnga::ui_qt
  pnga::png_format
  pnga::io
  Qt6::Test
)
target_compile_features(pnga_gui_chunk_model_tests PRIVATE cxx_std_20)

add_test(NAME gui_chunk_model_tests COMMAND pnga_gui_chunk_model_tests)

add_executable(pnga_gui_image_transform_tests
  image_transform_test.cpp
)
set_target_properties(pnga_gui_image_transform_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_image_transform_tests PRIVATE
  pnga::ui_qt
  Qt6::Test
)
target_compile_features(pnga_gui_image_transform_tests PRIVATE cxx_std_20)

add_test(NAME gui_image_transform_tests COMMAND pnga_gui_image_transform_tests)

add_executable(pnga_gui_delivered_image_view_interaction_tests
  delivered_image_view_interaction_test.cpp
)
set_target_properties(pnga_gui_delivered_image_view_interaction_tests PROPERTIES
  AUTOMOC ON
)
target_link_libraries(pnga_gui_delivered_image_view_interaction_tests PRIVATE
  pnga::ui_qt
  Qt6::Test
)
target_compile_features(pnga_gui_delivered_image_view_interaction_tests PRIVATE cxx_std_20)

add_test(NAME gui_delivered_image_view_interaction_tests
  COMMAND pnga_gui_delivered_image_view_interaction_tests)

add_executable(pnga_gui_pixel_viewport_tests
  pixel_viewport_test.cpp
)
set_target_properties(pnga_gui_pixel_viewport_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_pixel_viewport_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_pixel_viewport_tests PRIVATE cxx_std_20)

add_test(NAME gui_pixel_viewport_tests COMMAND pnga_gui_pixel_viewport_tests)

add_executable(pnga_gui_stage_preview_view_tests
  stage_preview_view_test.cpp
)
set_target_properties(pnga_gui_stage_preview_view_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_stage_preview_view_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_stage_preview_view_tests PRIVATE cxx_std_20)

add_test(NAME gui_stage_preview_view_tests COMMAND pnga_gui_stage_preview_view_tests)

add_executable(pnga_gui_stage_pixel_process_view_tests
  stage_pixel_process_view_test.cpp
)
set_target_properties(pnga_gui_stage_pixel_process_view_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_stage_pixel_process_view_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  Qt6::Test
)
target_compile_features(pnga_gui_stage_pixel_process_view_tests PRIVATE cxx_std_20)

add_test(NAME gui_stage_pixel_process_view_tests
  COMMAND pnga_gui_stage_pixel_process_view_tests)

add_executable(pnga_gui_hex_data_source_tests
  hex_data_source_test.cpp
)
set_target_properties(pnga_gui_hex_data_source_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_hex_data_source_tests PRIVATE
  pnga::ui_qt
  pnga::png_format
  pnga::io
  Qt6::Test
)
target_compile_features(pnga_gui_hex_data_source_tests PRIVATE cxx_std_20)

add_test(NAME gui_hex_data_source_tests COMMAND pnga_gui_hex_data_source_tests)

add_executable(pnga_gui_hex_source_tab_bar_tests
  hex_source_tab_bar_test.cpp
)
set_target_properties(pnga_gui_hex_source_tab_bar_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_hex_source_tab_bar_tests PRIVATE
  pnga::ui_qt
  Qt6::Test
)
target_compile_features(pnga_gui_hex_source_tab_bar_tests PRIVATE cxx_std_20)

add_test(NAME gui_hex_source_tab_bar_tests COMMAND pnga_gui_hex_source_tab_bar_tests)

add_executable(pnga_gui_selection_bus_tests
  selection_bus_test.cpp
)
set_target_properties(pnga_gui_selection_bus_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_selection_bus_tests PRIVATE
  pnga::ui_qt
  pnga::trace_model
  Qt6::Test
)
target_compile_features(pnga_gui_selection_bus_tests PRIVATE cxx_std_20)

add_test(NAME gui_selection_bus_tests COMMAND pnga_gui_selection_bus_tests)

add_executable(pnga_gui_selection_view_state_tests
  selection_view_state_test.cpp
)
set_target_properties(pnga_gui_selection_view_state_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_selection_view_state_tests PRIVATE
  pnga::ui_qt
  pnga::trace_model
  Qt6::Test
)
target_compile_features(pnga_gui_selection_view_state_tests PRIVATE cxx_std_20)

add_test(NAME gui_selection_view_state_tests COMMAND pnga_gui_selection_view_state_tests)

add_executable(pnga_gui_compression_selection_store_tests
  compression_selection_store_test.cpp
)
set_target_properties(pnga_gui_compression_selection_store_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_compression_selection_store_tests PRIVATE
  pnga::ui_qt
  pnga::trace_model
  Qt6::Test
)
target_compile_features(pnga_gui_compression_selection_store_tests PRIVATE cxx_std_20)

add_test(NAME gui_compression_selection_store_tests
  COMMAND pnga_gui_compression_selection_store_tests)

# WP-5U15: normalized application-source list shared by the three MainWindow
# test executables so extraction tasks keep source parity. Do not compile
# src/main.cpp into tests.
set(PNGA_GUI_APP_TEST_SOURCES
  ../../apps/png-analyzer-gui/src/main_window.cpp
  ../../apps/png-analyzer-gui/src/document_workers.cpp
  ../../apps/png-analyzer-gui/src/main_window_ui.cpp
  ../../apps/png-analyzer-gui/src/workspace_controller.cpp
  ../../apps/png-analyzer-gui/src/document_session.cpp
  ../../apps/png-analyzer-gui/src/selection_navigation_controller.cpp
  ../../apps/png-analyzer-gui/src/trace_controller.cpp
)

# WP-5U15 focused-test helper: each extraction unit gets one executable that
# compiles the full normalized application source list plus its own test.
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

add_pnga_gui_app_test(pnga_gui_document_workers_tests
  document_workers_test.cpp gui_document_workers_tests)

add_pnga_gui_app_test(pnga_gui_main_window_ui_tests
  main_window_ui_test.cpp gui_main_window_ui_tests)

add_pnga_gui_app_test(pnga_gui_workspace_controller_tests
  workspace_controller_test.cpp gui_workspace_controller_tests)

add_pnga_gui_app_test(pnga_gui_document_session_tests
  document_session_test.cpp gui_document_session_tests)

add_pnga_gui_app_test(pnga_gui_selection_navigation_controller_tests
  selection_navigation_controller_test.cpp
  gui_selection_navigation_controller_tests)

add_pnga_gui_app_test(pnga_gui_trace_controller_tests
  trace_controller_test.cpp gui_trace_controller_tests)
target_compile_definitions(pnga_gui_trace_controller_tests PRIVATE
  PNGA_TRACE_CONTROLLER_TESTING=1)

add_executable(pnga_gui_main_window_layout_tests
  main_window_layout_test.cpp
  ${PNGA_GUI_APP_TEST_SOURCES}
)
set_target_properties(pnga_gui_main_window_layout_tests PROPERTIES AUTOMOC ON)
target_include_directories(pnga_gui_main_window_layout_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/png-analyzer-gui/src
)
target_link_libraries(pnga_gui_main_window_layout_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
  Qt6::Test
)
target_compile_features(pnga_gui_main_window_layout_tests PRIVATE cxx_std_20)

add_test(NAME gui_main_window_layout_tests COMMAND pnga_gui_main_window_layout_tests)

# WP-5U15: enforce the facade line gates whenever the layout test builds.
add_custom_command(TARGET pnga_gui_main_window_layout_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DMAIN_WINDOW_CPP=${CMAKE_SOURCE_DIR}/apps/png-analyzer-gui/src/main_window.cpp
    -DMAIN_WINDOW_H=${CMAKE_SOURCE_DIR}/apps/png-analyzer-gui/src/main_window.h
    -P ${CMAKE_SOURCE_DIR}/tests/gui/check_main_window_line_budget.cmake
  VERBATIM
)

add_executable(pnga_gui_trace_pipeline_integration_tests
  trace_pipeline_integration_test.cpp
  ${PNGA_GUI_APP_TEST_SOURCES}
)
set_target_properties(pnga_gui_trace_pipeline_integration_tests PROPERTIES AUTOMOC ON)
target_include_directories(pnga_gui_trace_pipeline_integration_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/png-analyzer-gui/src
)
target_link_libraries(pnga_gui_trace_pipeline_integration_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
  Qt6::Test
)
target_compile_features(pnga_gui_trace_pipeline_integration_tests PRIVATE cxx_std_20)

add_test(NAME gui_trace_pipeline_integration_tests
  COMMAND pnga_gui_trace_pipeline_integration_tests)

# WP-607C: real-file corpus gate. Opens generated corpus PNGs through
# MainWindow::openFile() and asserts state, width and error behavior; depends
# on the wp607c-generated-corpus CTest fixture for the build-tree files.
add_executable(pnga_gui_wp607c_corpus_tests
  controlled_corpus_gate_test.cpp
  ${PNGA_GUI_APP_TEST_SOURCES}
)
set_target_properties(pnga_gui_wp607c_corpus_tests PROPERTIES AUTOMOC ON)
target_include_directories(pnga_gui_wp607c_corpus_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/png-analyzer-gui/src
)
target_link_libraries(pnga_gui_wp607c_corpus_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
  Qt6::Test
)
target_compile_features(pnga_gui_wp607c_corpus_tests PRIVATE cxx_std_20)
target_compile_definitions(pnga_gui_wp607c_corpus_tests PRIVATE
  PNGA_WP607C_CORPUS_DIR="${CMAKE_BINARY_DIR}/tests/corpus/wp-607c")

add_test(NAME gui_wp607c_corpus_tests
  COMMAND pnga_gui_wp607c_corpus_tests)
set_tests_properties(gui_wp607c_corpus_tests PROPERTIES
  FIXTURES_REQUIRED wp607c-generated-corpus
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  LABELS "corpus;gui;wp607c")

add_executable(pnga_gui_compression_inspector_responsive_tests
  compression_inspector_responsive_test.cpp
)
set_target_properties(pnga_gui_compression_inspector_responsive_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_compression_inspector_responsive_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_compression_inspector_responsive_tests PRIVATE cxx_std_20)

add_test(NAME gui_compression_inspector_responsive_tests
  COMMAND pnga_gui_compression_inspector_responsive_tests)

# WP-5U12F product gate: drives the real three-page inspector over the
# WP-607C corpus, captures the plan-pinned baseline matrix under build/ and
# asserts the state/width/theme/action/no-replay contract. The counters are
# exposed only through PNGA_TRACE_CONTROLLER_TESTING.
add_pnga_gui_app_test(pnga_gui_compression_inspector_product_gate_tests
  compression_inspector_product_gate_test.cpp
  gui_compression_inspector_product_gate_tests)
target_compile_definitions(pnga_gui_compression_inspector_product_gate_tests PRIVATE
  PNGA_TRACE_CONTROLLER_TESTING=1
  PNGA_WP607C_CORPUS_DIR="${CMAKE_BINARY_DIR}/tests/corpus/wp-607c")
set_tests_properties(gui_compression_inspector_product_gate_tests PROPERTIES
  FIXTURES_REQUIRED wp607c-generated-corpus
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  LABELS "gui;wp607c;wp5u12")

add_executable(pnga_gui_cross_platform_gate_tests
  cross_platform_gui_gate_test.cpp
  ${PNGA_GUI_APP_TEST_SOURCES}
)
set_target_properties(pnga_gui_cross_platform_gate_tests PROPERTIES AUTOMOC ON)
target_include_directories(pnga_gui_cross_platform_gate_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/png-analyzer-gui/src
)
target_link_libraries(pnga_gui_cross_platform_gate_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
  Qt6::Test
)
target_compile_features(pnga_gui_cross_platform_gate_tests PRIVATE cxx_std_20)

add_test(NAME gui_cross_platform_gate_tests
  COMMAND pnga_gui_cross_platform_gate_tests)
set_tests_properties(gui_cross_platform_gate_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME gui_cross_platform_gate_dpi_150_tests
  COMMAND pnga_gui_cross_platform_gate_tests)
set_tests_properties(gui_cross_platform_gate_dpi_150_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1.5;QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough")
add_test(NAME gui_cross_platform_gate_dpi_200_tests
  COMMAND pnga_gui_cross_platform_gate_tests)
set_tests_properties(gui_cross_platform_gate_dpi_200_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2;QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough")

add_executable(pnga_gui_stage_inspector_tests
  stage_inspector_test.cpp
)
set_target_properties(pnga_gui_stage_inspector_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_stage_inspector_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  PNG::PNG
  ZLIB::ZLIB
  Qt6::Test
)
target_include_directories(pnga_gui_stage_inspector_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_compile_features(pnga_gui_stage_inspector_tests PRIVATE cxx_std_20)

add_test(NAME gui_stage_inspector_tests COMMAND pnga_gui_stage_inspector_tests)

add_executable(pnga_gui_block_inspector_tests
  block_inspector_test.cpp
)
set_target_properties(pnga_gui_block_inspector_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_block_inspector_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_block_inspector_tests PRIVATE cxx_std_20)

add_test(NAME gui_block_inspector_tests COMMAND pnga_gui_block_inspector_tests)

add_executable(pnga_gui_huffman_inspector_tests
  huffman_inspector_test.cpp
)
set_target_properties(pnga_gui_huffman_inspector_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_huffman_inspector_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_huffman_inspector_tests PRIVATE cxx_std_20)

add_test(NAME gui_huffman_inspector_tests COMMAND pnga_gui_huffman_inspector_tests)

add_executable(pnga_gui_decode_trace_inspector_tests
  decode_trace_inspector_test.cpp
)
set_target_properties(pnga_gui_decode_trace_inspector_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_decode_trace_inspector_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_decode_trace_inspector_tests PRIVATE cxx_std_20)

add_test(NAME gui_decode_trace_inspector_tests
  COMMAND pnga_gui_decode_trace_inspector_tests)

add_executable(pnga_gui_trace_inspector_binding_tests
  trace_inspector_binding_test.cpp
)
set_target_properties(pnga_gui_trace_inspector_binding_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_trace_inspector_binding_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_trace_inspector_binding_tests PRIVATE cxx_std_20)

add_test(NAME gui_trace_inspector_binding_tests
  COMMAND pnga_gui_trace_inspector_binding_tests)

add_executable(pnga_gui_trace_inspector_performance_tests
  trace_inspector_performance_test.cpp
)
set_target_properties(pnga_gui_trace_inspector_performance_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_trace_inspector_performance_tests PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  Qt6::Test
)
target_compile_features(pnga_gui_trace_inspector_performance_tests PRIVATE cxx_std_20)

add_test(NAME gui_trace_inspector_performance_tests
  COMMAND pnga_gui_trace_inspector_performance_tests)

add_executable(pnga_gui_about_tests
  about_dialog_test.cpp
)
set_target_properties(pnga_gui_about_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_about_tests PRIVATE
  pnga::ui_qt
  pnga::core
  Qt6::Test
)
target_compile_features(pnga_gui_about_tests PRIVATE cxx_std_20)

add_test(NAME gui_about_tests COMMAND pnga_gui_about_tests)

add_executable(pnga_gui_application_theme_tests
  application_theme_test.cpp
)
set_target_properties(pnga_gui_application_theme_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_gui_application_theme_tests PRIVATE
  pnga::ui_qt
  Qt6::Test
)
target_compile_features(pnga_gui_application_theme_tests PRIVATE cxx_std_20)

add_test(NAME gui_application_theme_tests COMMAND pnga_gui_application_theme_tests)
set_tests_properties(gui_application_theme_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# WP-606 verification target: build every GUI test executable without running
# them so the documented two-target build command remains deterministic.
add_custom_target(pnga_gui_tests DEPENDS
  pnga_gui_chunk_model_tests
  pnga_gui_image_transform_tests
  pnga_gui_delivered_image_view_interaction_tests
  pnga_gui_pixel_viewport_tests
  pnga_gui_stage_preview_view_tests
  pnga_gui_hex_data_source_tests
  pnga_gui_selection_bus_tests
  pnga_gui_selection_view_state_tests
  pnga_gui_compression_selection_store_tests
  pnga_gui_main_window_layout_tests
  pnga_gui_trace_pipeline_integration_tests
  pnga_gui_wp607c_corpus_tests
  pnga_gui_compression_inspector_responsive_tests
  pnga_gui_compression_inspector_product_gate_tests
  pnga_gui_cross_platform_gate_tests
  pnga_gui_stage_inspector_tests
  pnga_gui_block_inspector_tests
  pnga_gui_huffman_inspector_tests
  pnga_gui_decode_trace_inspector_tests
  pnga_gui_trace_inspector_binding_tests
  pnga_gui_trace_inspector_performance_tests
  pnga_gui_about_tests
  pnga_gui_application_theme_tests
)
