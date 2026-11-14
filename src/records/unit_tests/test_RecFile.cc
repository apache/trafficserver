/** @file

   Catch-based tests for RecFile.cc

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

#include <catch2/catch_test_macros.hpp>

#include "../P_RecFile.h"

#include <unistd.h>

TEST_CASE("RecFileGetSize", "[librecords][RecFile]")
{
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  close(fds[1]);

  SECTION("an open descriptor reports its size")
  {
    REQUIRE(RecFileGetSize(fds[0]) == 0);
    close(fds[0]);
  }

  SECTION("a descriptor that cannot be stat'ed reports failure")
  {
    close(fds[0]);
    REQUIRE(RecFileGetSize(fds[0]) == -1);
  }
}
