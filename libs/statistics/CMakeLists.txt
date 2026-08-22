# libs/statistics — bounded, deterministic, Qt-free aggregation (WP-602A).

add_library(pnga_statistics
  src/statistics.cpp
)
add_library(pnga::statistics ALIAS pnga_statistics)

target_include_directories(pnga_statistics PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_statistics PUBLIC
  pnga::core
  pnga::trace_model
)
target_compile_features(pnga_statistics PUBLIC cxx_std_20)
set_target_properties(pnga_statistics PROPERTIES POSITION_INDEPENDENT_CODE ON)
