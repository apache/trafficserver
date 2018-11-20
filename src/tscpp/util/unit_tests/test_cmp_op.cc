/** @file

    Unit tests for cmp_op.h.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "catch.hpp"
#include "tscpp/util/cmp_op.h"

#include <cstring>

struct A {
  int i;
};

int
cmpA(const A &op1, const A &op2)
{
  return op1.i - op2.i;
}

int
cmpAi(const A &op1, const int &op2)
{
  return op1.i - op2;
}

namespace ts
{
namespace cmp_op
{
  template <> struct Enable<A, A> : public Yes<A, A, cmpA> {
  };

  template <> struct Enable<A, int> : public Yes<A, int, cmpAi> {
  };
} // namespace cmp_op
} // namespace ts

namespace
{
template <typename T1, typename T2>
bool
tst(T1 op1, T2 op2, const char goal[6])
{
  char r[6];

  r[0] = op1 == op2 ? 'Y' : 'N';
  r[1] = op1 != op2 ? 'Y' : 'N';
  r[2] = op1 > op2 ? 'Y' : 'N';
  r[3] = op1 >= op2 ? 'Y' : 'N';
  r[4] = op1 < op2 ? 'Y' : 'N';
  r[5] = op1 <= op2 ? 'Y' : 'N';

  return std::memcmp(r, goal, 6) == 0;
}

const char G[] = "NYYYNN";
const char E[] = "YNNYNY";
const char L[] = "NYNNYY";

} // end anonymous namespace

TEST_CASE("cmp_op", "[CMPOP]")
{
  A x, y;

  x.i = 2;
  y.i = 1;
  REQUIRE(tst<A, A>(x, y, G));

  x.i = 1;
  y.i = 1;
  REQUIRE(tst<A, A>(x, y, E));

  x.i = 1;
  y.i = 2;
  REQUIRE(tst<A, A>(x, y, L));

  x.i = 2;
  REQUIRE(tst<A, int>(x, 1, G));

  x.i = 1;
  REQUIRE(tst<A, int>(x, 1, E));

  x.i = 1;
  REQUIRE(tst<A, int>(x, 2, L));

  x.i = 1;
  REQUIRE(tst<int, A>(2, x, G));

  x.i = 1;
  REQUIRE(tst<int, A>(1, x, E));

  x.i = 2;
  REQUIRE(tst<int, A>(1, x, L));

  x.i = 2;
  y.i = 1;
  REQUIRE(tst<const A, const A>(x, y, G));

  x.i = 1;
  y.i = 1;
  REQUIRE(tst<const A, const A>(x, y, E));

  x.i = 1;
  y.i = 2;
  REQUIRE(tst<const A, const A>(x, y, L));

  x.i = 2;
  REQUIRE(tst<const A, const int>(x, 1, G));

  x.i = 1;
  REQUIRE(tst<const A, const int>(x, 1, E));

  x.i = 1;
  REQUIRE(tst<const A, const int>(x, 2, L));

  x.i = 1;
  REQUIRE(tst<const int, const A>(2, x, G));

  x.i = 1;
  REQUIRE(tst<const int, const A>(1, x, E));

  x.i = 2;
  REQUIRE(tst<const int, const A>(1, x, L));

  x.i = 2;
  y.i = 1;
  REQUIRE(tst<const A &, const A &>(x, y, G));

  x.i = 1;
  y.i = 1;
  REQUIRE(tst<const A &, const A &>(x, y, E));

  x.i = 1;
  y.i = 2;
  REQUIRE(tst<const A &, const A &>(x, y, L));

  x.i = 2;
  REQUIRE(tst<const A &, const int &>(x, 1, G));

  x.i = 1;
  REQUIRE(tst<const A &, const int &>(x, 1, E));

  x.i = 1;
  REQUIRE(tst<const A &, const int &>(x, 2, L));

  x.i = 1;
  REQUIRE(tst<const int &, const A &>(2, x, G));

  x.i = 1;
  REQUIRE(tst<const int &, const A &>(1, x, E));

  x.i = 2;
  REQUIRE(tst<const int &, const A &>(1, x, L));

  x.i = 2;
  y.i = 1;
  REQUIRE(tst<A &, A &>(x, y, G));

  x.i = 1;
  y.i = 1;
  REQUIRE(tst<A &, A &>(x, y, E));

  x.i = 1;
  y.i = 2;
  REQUIRE(tst<A &, A &>(x, y, L));

  x.i = 2;
  REQUIRE(tst<A &, int>(x, 1, G));

  x.i = 1;
  REQUIRE(tst<A &, int>(x, 1, E));

  x.i = 1;
  REQUIRE(tst<A &, int>(x, 2, L));

  x.i = 1;
  REQUIRE(tst<int, A &>(2, x, G));

  x.i = 1;
  REQUIRE(tst<int, A &>(1, x, E));

  x.i = 2;
  REQUIRE(tst<int, A &>(1, x, L));
}
