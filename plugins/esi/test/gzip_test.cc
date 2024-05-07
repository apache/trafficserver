/** @file

  A brief file description

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

#include <string>
#include <cstring>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "Utils.h"
#include "gzip.h"

using std::string;
using namespace EsiLib;

TEST_CASE("test esi plugin - gzip")
{
  SECTION("===================== Test 1")
  {
    const char expected_cdata[] = "\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03\xf3\x48\xcd\xc9\xc9\x57\x08\xcf\x2f\xca\x49\x51\x04\x00"
                                  "\xa3\x1c\x29\x1c\x0c\x00\x00\x00";
    const char expected_data[]  = "Hello World!";

    string cdata;
    // check output of gzip
    REQUIRE(gzip(expected_data, 12, cdata));

    // check the size of compressed data
    REQUIRE(cdata.size() == 32);

    // check the content of compressed data
    REQUIRE(strncmp(expected_cdata, cdata.c_str(), cdata.size()) == 0);

    BufferList buf_list;
    string     data;
    // check output of gunzip
    REQUIRE(gunzip(expected_cdata, 32, buf_list));
    data = (buf_list.begin())->data();
    data = buf_list.front();

    // check the size of uncompressed data
    REQUIRE(data.size() == 12);

    // check the content of uncompressed data
    CHECK(strncmp(expected_data, data.c_str(), data.size()) == 0);
  }

  SECTION("===================== Test 2")
  {
    // OS_TYPE (byte[9]) is 0
    const char expected_cdata[] = "\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x00\xf3\x48\xcd\xc9\xc9\x57\x08\xcf\x2f\xca\x49\x51\x04\x00"
                                  "\xa3\x1c\x29\x1c\x0c\x00\x00\x00";
    const char expected_data[]  = "Hello World!";

    BufferList buf_list;
    string     data;
    // check output of gunzip
    REQUIRE(gunzip(expected_cdata, 32, buf_list));
    data = (buf_list.begin())->data();

    // check the size of uncompressed data
    REQUIRE(data.size() == 12);

    // check the content of uncompressed data
    CHECK(strncmp(expected_data, data.c_str(), data.size()) == 0);
  }

  SECTION("invalid compressed data - too short")
  {
    const char expected_cdata[] = "\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xa3\x1c\x29\x1c\x0c\x00\x00\x00";

    BufferList buf_list;
    // check output of gunzip
    CHECK(gunzip(expected_cdata, 17, buf_list) == false);
  }

  SECTION("invalid magic byte")
  {
    const char expected_cdata[] = "\x1f\x8c\x08\x00\x00\x00\x00\x00\x00\x00\xf3\x48\xcd\xc9\xc9\x57\x08\xcf\x2f\xca\x49\x51\x04\x00"
                                  "\xa3\x1c\x29\x1c\x0c\x00\x00\x00";

    BufferList buf_list;
    // check output of gunzip
    CHECK(gunzip(expected_cdata, 32, buf_list) == false);
  }
}
