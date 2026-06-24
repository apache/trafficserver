/** @file

  Unit tests for DNS-over-TCP read state.

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

#include "P_DNSConnection.h"

TEST_CASE("DNS TCP length prefix can be read one byte at a time", "[dns][tcp]")
{
  DNSConnection::TCPData tcp_data;
  unsigned char const    prefix[] = {0x12, 0x34};

  REQUIRE(tcp_data.length_prefix_bytes_remaining() == 2);
  REQUIRE_FALSE(tcp_data.length_prefix_is_complete());

  REQUIRE(tcp_data.append_length_prefix_bytes(prefix, 1) == 1);
  CHECK(tcp_data.length_read == 1);
  CHECK(tcp_data.length_prefix_bytes_remaining() == 1);
  CHECK_FALSE(tcp_data.length_prefix_is_complete());
  CHECK(tcp_data.total_length == 0);

  REQUIRE(tcp_data.append_length_prefix_bytes(prefix + 1, 1) == 1);
  CHECK(tcp_data.length_read == 2);
  CHECK(tcp_data.length_prefix_bytes_remaining() == 0);
  CHECK(tcp_data.length_prefix_is_complete());
  CHECK(tcp_data.total_length == 0x1234);
}

TEST_CASE("DNS TCP length prefix state clamps and resets", "[dns][tcp]")
{
  DNSConnection::TCPData tcp_data;
  unsigned char const    prefix[] = {0x00, 0x08, 0xff};

  REQUIRE(tcp_data.append_length_prefix_bytes(prefix, sizeof(prefix)) == 2);
  CHECK(tcp_data.length_prefix_bytes_remaining() == 0);
  CHECK(tcp_data.length_prefix_is_complete());
  CHECK(tcp_data.total_length == 8);

  tcp_data.done_reading = 4;
  tcp_data.reset();
  CHECK(tcp_data.length_read == 0);
  CHECK(tcp_data.length_prefix_bytes_remaining() == 2);
  CHECK_FALSE(tcp_data.length_prefix_is_complete());
  CHECK(tcp_data.total_length == 0);
  CHECK(tcp_data.done_reading == 0);
}
