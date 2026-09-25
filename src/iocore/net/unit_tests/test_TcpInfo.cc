/** @file

  Tests for collecting TCP_INFO from network connections.

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

#include "../P_UnixNetVConnection.h"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>

TEST_CASE("TCP_INFO skips UDP sockets without a syscall", "[net][tcpinfo]")
{
  UnixNetVConnection vc;
  NetVCOptions       options;
  TcpInfoSnapshot    info;

  options.ip_proto = NetVCOptions::USE_UDP;
  REQUIRE(vc.con.open(options) == 0);

  // TCP_INFO on a UDP socket would fail and set errno.
  errno                   = 0;
  bool const has_info     = vc.get_tcp_info(info);
  int const  socket_errno = errno;

  CHECK_FALSE(has_info);
  CHECK(socket_errno == 0);
}
