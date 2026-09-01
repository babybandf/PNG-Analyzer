# WP-5U15: mechanized facade line gates. The MainWindow facade must stay a
# thin composition root: at most 600 lines of implementation and 160 lines of
# header. Attached as a POST_BUILD step of pnga_gui_main_window_layout_tests.

foreach(file IN ITEMS "${MAIN_WINDOW_CPP}" "${MAIN_WINDOW_H}")
  file(STRINGS "${file}" lines ENCODING "UTF-8")
  list(LENGTH lines count)
  if(file STREQUAL "${MAIN_WINDOW_CPP}" AND count GREATER 600)
    message(FATAL_ERROR "main_window.cpp has ${count} lines; maximum is 600")
  endif()
  if(file STREQUAL "${MAIN_WINDOW_H}" AND count GREATER 160)
    message(FATAL_ERROR "main_window.h has ${count} lines; maximum is 160")
  endif()
endforeach()
