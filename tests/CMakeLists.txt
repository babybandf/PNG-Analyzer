# Central test registry (layout contract §11). Each module owns a
# tests/unit/<module>/ subtree; only tests for modules that exist are added.

add_subdirectory(unit/core)
add_subdirectory(unit/io)
add_subdirectory(unit/png-format)
add_subdirectory(unit/validation)
add_subdirectory(unit/trace-model)
add_subdirectory(unit/backend-libpng)
add_subdirectory(unit/analysis-engine)
add_subdirectory(integration/cli)
if(PNGA_HAVE_QT)
  add_subdirectory(gui)
endif()