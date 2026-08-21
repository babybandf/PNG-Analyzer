# Central test registry (layout contract §11). Each module owns a
# tests/unit/<module>/ subtree; only tests for modules that exist are added.

add_subdirectory(unit/core)
add_subdirectory(unit/io)
add_subdirectory(unit/png-format)
add_subdirectory(integration/cli)