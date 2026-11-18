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

#include <cstdlib>
#include <unistd.h>

TEST_CASE("RecFileGetSize", "[librecords][RecFile]")
{
  char path[] = "/tmp/test_RecFile.XXXXXX";
  int  fd     = mkstemp(path);
  REQUIRE(fd >= 0);
  unlink(path);
  REQUIRE(write(fd, "12345", 5) == 5);

  SECTION("an open descriptor reports its size")
  {
    REQUIRE(RecFileGetSize(fd) == 5);
    close(fd);
  }

  SECTION("a descriptor that cannot be stat'ed reports failure")
  {
    close(fd);
    REQUIRE(RecFileGetSize(-1) == -1);
  }
}
