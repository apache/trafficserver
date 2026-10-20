// SPDX-License-Identifier: Apache-2.0
/** @file

    MemSpan unit tests.

*/

#include <iostream>
#include "swoc/Vectray.h"
#include <catch2/catch_test_macros.hpp>

using swoc::Vectray;

TEST_CASE("Vectray", "[libswoc][Vectray]") {
  struct Thing {
    unsigned n               = 56;
    Thing()                  = default;
    Thing(Thing const &that) = default;
    Thing(Thing &&that) : n(that.n) { that.n = 0; }
    Thing(unsigned u) : n(u) {}
  };

  Vectray<Thing, 1> unit_thing;
  Thing PhysicalThing{0};

  REQUIRE(unit_thing.size() == 0);

  unit_thing.push_back(PhysicalThing); // Copy construct
  REQUIRE(unit_thing.size() == 1);
  unit_thing.push_back(Thing{1});
  REQUIRE(unit_thing.size() == 2);
  unit_thing.push_back(Thing{2});
  REQUIRE(unit_thing.size() == 3);

  // Check via indexed access.
  for (unsigned idx = 0; idx < unit_thing.size(); ++idx) {
    REQUIRE(unit_thing[idx].n == idx);
  }

  // Check via container access.
  unsigned n = 0;
  for (auto const &thing : unit_thing) {
    REQUIRE(thing.n == n);
    ++n;
  }
  REQUIRE(n == unit_thing.size());

  Thing tmp{99};
  unit_thing.push_back(std::move(tmp));
  REQUIRE(unit_thing[3].n == 99);
  REQUIRE(tmp.n == 0);
  PhysicalThing.n = 101;
  unit_thing.push_back(PhysicalThing);
  REQUIRE(unit_thing.back().n == 101);
  REQUIRE(PhysicalThing.n == 101);
}

TEST_CASE("Vectray Destructor", "[libswoc][Vectray]") {
  int count = 0;
  struct Q {
    int &count_;
    Q(int &count) : count_(count) {}
    ~Q() { ++count_; }
  };

  {
    Vectray<Q, 1> v1;
    v1.emplace_back(count);
  }
  REQUIRE(count == 1);

  count = 0;
  { // force use of dynamic memory.
    Vectray<Q, 1> v2;
    v2.emplace_back(count);
    v2.emplace_back(count);
    v2.emplace_back(count);
  }
  // Hard to get an exact cound because of std::vector resizes.
  // But first object should be at least double deleted because of transfer.
  REQUIRE(count >= 4);
}

TEST_CASE("Vectray iteration skips empty slots", "[libswoc][Vectray]") {
  Vectray<int, 4> v;
  v.push_back(1);
  v.push_back(2);

  REQUIRE(v.end() - v.begin() == 2);
  unsigned n = 0;
  for (auto const &item : v) {
    REQUIRE(item == int(++n));
  }
  REQUIRE(n == v.size());
}

TEST_CASE("Vectray destructor doesn't destroy empty slots", "[libswoc][Vectray]") {
  int count = 0;
  struct Q {
    int &count_;
    Q(int &count) : count_(count) {}
    ~Q() { ++count_; }
  };

  {
    Vectray<Q, 4> vq;
    vq.emplace_back(count);
  }
  REQUIRE(count == 1);
}
