/** @file

    Unit tests for OpsFromCmp.h.

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
#include "tscpp/util/OpsFromCmp.h"

#include <cstring>

namespace Dummy
{
struct A {
  int i;
};

int
cmp(const A &op1, const A &op2)
{
  return op1.i - op2.i;
}

TS_DEFINE_CMP_OPS(A)

int
cmp(const A &op1, int op2)
{
  return op1.i - op2;
}

TS_DEFINE_CMP_OPS_2T(A, int)

} // end namespace Dummy

namespace
{
template <typename T1, typename T2>
bool
tst(const T1 &op1, const T2 &op2, const char goal[6])
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

TEST_CASE("OpsFromCmp", "[OFC]")
{
  Dummy::A x, y;

  x.i = 2;
  y.i = 1;
  REQUIRE(tst(x, y, G));

  x.i = 1;
  y.i = 1;
  REQUIRE(tst(x, y, E));

  x.i = 1;
  y.i = 2;
  REQUIRE(tst(x, y, L));

  x.i = 2;
  REQUIRE(tst(x, 1, G));

  x.i = 1;
  REQUIRE(tst(x, 1, E));

  x.i = 1;
  REQUIRE(tst(x, 2, L));

  x.i = 1;
  REQUIRE(tst(2, x, G));

  x.i = 1;
  REQUIRE(tst(1, x, E));

  x.i = 2;
  REQUIRE(tst(1, x, L));
}
