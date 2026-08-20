#include <pnga/core/version.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>

TEST_CASE("core version string is present and stable") {
  const char* v = pnga::version_string();
  REQUIRE(v != nullptr);
  REQUIRE(std::strlen(v) > 0);
  REQUIRE(std::strcmp(v, PNGA_VERSION_STR) == 0);
}

TEST_CASE("core version macros describe the published version") {
  REQUIRE(PNGA_VERSION_MAJOR >= 0);
  REQUIRE(PNGA_VERSION_MINOR >= 0);
  REQUIRE(PNGA_VERSION_PATCH >= 0);
  REQUIRE(PNGA_VERSION_STR[0] != 0);
}