/** @file

    Unit tests for PostScript.h.

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
#include "tscpp/util/PostScript.h"

namespace
{
int f1Called;
int f2Called;
int f3Called;

int dummy;

void
f1(int a, double b, int *c, int &d)
{
  ++f1Called;

  REQUIRE(a == 1);
  REQUIRE(b == 2.0);
  REQUIRE(c == &dummy);
  REQUIRE(&d == &dummy);
}

void
f2(double a)
{
  ++f2Called;
}

void
f3(int a, double b)
{
  ++f3Called;

  REQUIRE(a == 5);
  REQUIRE(b == 6.0);
}

} // end anonymous namespace

TEST_CASE("PostScript", "[PSC]")
{
  {
    int *p = &dummy;
    ts::PostScript g1([&]() -> void { f1(1, 2.0, p, dummy); });
    ts::PostScript g2([=]() -> void { f2(4); });
    ts::PostScript g3([=]() -> void { f3(5, 6.0); });

    g2.release();
  }

  REQUIRE(f1Called == 1);
  REQUIRE(f2Called == 0);
  REQUIRE(f3Called == 1);
}
