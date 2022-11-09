#include <string_view>
#define CATCH_CONFIG_MAIN /* include main function */
#include <catch.hpp>      /* catch unit-test framework */
#include "../utils.h"

using std::string_view;
TEST_CASE("Util methods", "[authproxy][utility]")
{
  SECTION("ContainsPrefix()")
  {
    CHECK(ContainsPrefix(string_view{"abcdef"}, string_view{"abc"}) == true);
    CHECK(ContainsPrefix(string_view{"abc"}, string_view{"abcdef"}) == false);
    CHECK(ContainsPrefix(string_view{"abcdef"}, string_view{"abd"}) == false);
    CHECK(ContainsPrefix(string_view{"abc"}, string_view{"abc"}) == true);
    CHECK(ContainsPrefix(string_view{""}, string_view{""}) == true);
    CHECK(ContainsPrefix(string_view{"abc"}, string_view{""}) == true);
    CHECK(ContainsPrefix(string_view{""}, string_view{"abc"}) == false);
    CHECK(ContainsPrefix(string_view{"abcdef"}, string_view{"abc\0"}) == true);
    CHECK(ContainsPrefix(string_view{"abcdef\0"}, string_view{"abc\0"}) == true);
  }
}