/** @file

    Unit tests for ts::Comparable.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <cstring>

#include "tscpp/util/Comparable.h"
#include "tscpp/util/TextView.h"

#include "catch.hpp"

struct Alpha : public ts::Comparable {
  explicit Alpha(int x) : _n(x) {}
  int _n{0};
};

int
cmp(Alpha const &lhs, Alpha const &rhs)
{
  return lhs._n - rhs._n;
}

int
cmp(Alpha const &lhs, int rhs)
{
  return lhs._n - rhs;
}

int
cmp(int lhs, Alpha const &rhs)
{
  return lhs - rhs._n;
}

struct Bravo : public ts::Comparable {
  explicit Bravo(float x) : _f(x) {}
  float _f{0};

  float
  cmp(Bravo const &that) const
  {
    return _f - that._f;
  }
};

int
cmp(Alpha const &lhs, Bravo const &rhs)
{
  return lhs._n < rhs._f ? -1 : lhs._n > rhs._f ? 1 : 0;
}

int
cmp(Bravo const &lhs, Alpha const &rhs)
{
  return lhs._f < rhs._n ? -1 : lhs._f > rhs._n ? 1 : 0;
}

struct Charlie : public ts::Comparable {
  explicit Charlie(intmax_t x) : _n(x) {}

  intmax_t _n{0};

  intmax_t
  cmp(Charlie const &that) const
  {
    return _n - that._n;
  }
  int
  cmp(int x) const
  {
    return _n - x;
  }
};

struct Delta : public ts::Comparable {
  explicit Delta(std::string_view const &s) : _s(s) {}

  std::string _s;

  int
  cmp(std::string_view const &x) const
  {
    return ts::strcmp(_s, x);
  }

  // Verify we can override the use of `cmp`.
  int
  self_cmp(Delta const &that) const
  {
    return ts::strcmp(_s, that._s);
  }
};

// Tell Comparable to use self_cmp instead of cmp.
template <> struct ts::ComparablePolicy<Delta, Delta> {
  int
  operator()(Delta const &lhs, Delta const &rhs) const
  {
    return lhs.self_cmp(rhs);
  }
};

// Test inheritance
struct Echo : public Charlie {
  explicit Echo(intmax_t x) : Charlie{x} {}
};

int
cmp(Echo const &lhs, float x)
{
  return lhs._n < x ? -1 : lhs._n > x ? 1 : 0;
}

// More inheritance testing, this time using virtual base classes.
struct Foxtrot : public virtual ts::Comparable {
  Foxtrot(unsigned n) : _n(n) {}

  unsigned _n{0};
};

int
cmp(Foxtrot const &lhs, Foxtrot const &rhs)
{
  return lhs._n < rhs._n ? -1 : lhs._n > rhs._n ? 1 : 0;
}

struct Golf : public Foxtrot, public virtual ts::Comparable {
  Golf(unsigned n) : Foxtrot(n) {}
};

TEST_CASE("Comparable", "[meta][comparable]")
{
  Alpha a1{1};
  Alpha a2{2};
  Bravo b1{1.5};
  Charlie c1{3};
  Charlie c2{5};
  Delta d1{"sepideh"};
  Delta d2{"persia"};
  Echo e1{4};
  Foxtrot f1{10};
  Golf g1{9};

  REQUIRE(a1 == a1);
  REQUIRE(a1 == 1);
  REQUIRE(1 == a1);
  REQUIRE(a1 != a2);
  REQUIRE(a1 < a2);
  REQUIRE(a2 > a1);

  REQUIRE(c1 == c1);
  REQUIRE(c1 != c2);
  REQUIRE(c1 < c2);
  REQUIRE(c2 > c1);
  REQUIRE(c1 == 3);
  REQUIRE(3 == c1);

  // check that we didn't break the non-overloaded operators.
  REQUIRE(1 != 3);
  REQUIRE(3 != 1);

  REQUIRE(b1 < a2);
  REQUIRE(b1 > a1);
  REQUIRE(a2 > b1);
  REQUIRE(a1 < b1);

  REQUIRE(d1 < "zephyr");
  REQUIRE(d1 > "alpha");
  REQUIRE(d1 == "sepideh");
  REQUIRE(d1 == std::string_view{"sepideh"});
  // Verify the flip side.
  REQUIRE("zephyr" > d1);
  REQUIRE("alpha" < d1);
  REQUIRE("sepideh" == d1);
  REQUIRE(std::string_view{"sepideh"} == d1);
  REQUIRE(ts::TextView{"sepideh"} == d1);

  REQUIRE(d1 != d2);
  REQUIRE(d1 > d2);
  REQUIRE(d2 < d1);

  REQUIRE(e1 > 3.5);
  REQUIRE(e1 > 3);
  REQUIRE(e1 > c1);

  REQUIRE(f1 == f1);
  REQUIRE(f1 > g1);
  REQUIRE(g1 <= f1);
}
