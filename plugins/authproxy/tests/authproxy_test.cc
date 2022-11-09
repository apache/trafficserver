#include <string_view>
#define CATCH_CONFIG_MAIN /* include main function */
#include <catch.hpp>      /* catch unit-test framework */
#include "../utils.h"

using std::string_view;

TEST_CASE("ContainsPrefix(): contains prefix", "[authproxy][utility]")
{
  CHECK(ContainsPrefix(string_view{"abcdef"}, string_view{"abc"}) == true);
}

// TEST_CASE("ContainsPrefix(): contains prefix", "[authproxy][utility]")
// {
//     CHECK(ContainsPrefix("abc", "abcdef") == false);
// }

// TEST_CASE("ContainsPrefix(): contains prefix", "[authproxy][utility]")
// {
//     CHECK(ContainsPrefix("abc", "abc") == true);

// }
