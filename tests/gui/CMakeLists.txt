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
