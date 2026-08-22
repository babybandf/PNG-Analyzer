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

add_executable(pnga_gui_main_window_layout_tests
  main_window_layout_test.cpp
  ../../apps/png-analyzer-gui/src/main_window.cpp
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
