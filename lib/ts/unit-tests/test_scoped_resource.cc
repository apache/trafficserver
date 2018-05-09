/** @file

    IpMap unit tests.

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

#include <ts/ink_memory.h>
#include <catch.hpp>

ats_scoped_fd
fixed_fd()
{
  ats_scoped_fd zret{5};
  return zret;
}

ats_scoped_fd
direct_fixed_fd()
{
  return ats_scoped_fd{6};
}

TEST_CASE("scoped_resource", "[libts][scoped]")
{
  ats_scoped_fd no_fd;
  REQUIRE(-1 == no_fd);
  ats_scoped_fd fd1{fixed_fd()};
  REQUIRE(fd1 == 5);
  ats_scoped_fd fd2{direct_fixed_fd()};
  REQUIRE(6 == fd2);
  ats_scoped_fd fd3{ats_scoped_fd()};
  REQUIRE(-1 == fd3);
};
