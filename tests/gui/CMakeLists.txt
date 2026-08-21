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
