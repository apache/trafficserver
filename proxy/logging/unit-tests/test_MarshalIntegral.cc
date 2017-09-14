/** @file

  Catch-based tests for MarshalIntegral.h.

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

#include <cstdint>

#include <ts/BufferWriter.h>

#include <MarshalIntegral.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace LogUtils;

namespace
{
std::size_t highWater{0};

template <typename IT>
bool
test(IT multiplier, int count)
{
  IT un;

  for (IT val = 1; count--;) {
    ts::LocalBufferWriter<20> bw;

    marshalInsert(bw, val);

    if (bw.size() > highWater) {
      highWater = bw.size();
    }

    unmarshalFromArr(bw.view().data(), un);

    if (un != val) {
      return false;
    }

    val *= multiplier;
  }

  return true;
}

} // end annonymous namespace

TEST_CASE("LogUtilsMarshalIntegral", "[LUMI]")
{
  REQUIRE(test<signed char>(3, 6) == true);
  REQUIRE(highWater == 2);
  REQUIRE(test<int>(-3, 22) == true);
  REQUIRE(highWater == 5);
  REQUIRE(test<unsigned>(3, 22) == true);
  REQUIRE(highWater == 5);
  REQUIRE(test<std::int64_t>(-3, 43) == true);
  REQUIRE(highWater == 10);
}

// Mock.

#include <cstdlib>
#include <iostream>

void
_ink_assert(const char *a, const char *f, int line)
{
  std::cout << a << '\n' << f << '\n' << line << '\n';

  std::exit(1);
}
