// SPDX-License-Identifier: Apache-2.0
/** @file
 * Test Discrete Range.
 */

#include "swoc/DiscreteRange.h"
#include "swoc/TextView.h"
#include "catch.hpp"

using swoc::TextView;
using namespace std::literals;
using namespace swoc::literals;

using range_t = swoc::DiscreteRange<unsigned>;
TEST_CASE("Discrete Range", "[libswoc][range]") {
  range_t none; // empty range.
  range_t single{56};
  range_t r1{56, 100};
  range_t r2{101, 200};
  range_t r3{100, 200};

  REQUIRE(single.contains(56));
  REQUIRE_FALSE(single.contains(100));
  REQUIRE(r1.is_adjacent_to(r2));
  REQUIRE(r2.is_adjacent_to(r1));
  REQUIRE(r1.is_left_adjacent_to(r2));
  REQUIRE_FALSE(r2.is_left_adjacent_to(r1));

  REQUIRE(r2.is_subset_of(r3));
  REQUIRE(r3.is_superset_of(r2));

  REQUIRE_FALSE(r3.is_subset_of(r2));
  REQUIRE_FALSE(r2.is_superset_of(r3));

  REQUIRE(r2.is_subset_of(r2));
  REQUIRE_FALSE(r2.is_strict_subset_of(r2));
  REQUIRE(r3.is_superset_of(r3));
  REQUIRE_FALSE(r3.is_strict_superset_of(r3));
}
