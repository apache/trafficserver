/** @file

  Catch based unit tests for ServerSessionPool reuse matching

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

#include "proxy/http/HttpSessionManager.h"

TEST_CASE("ServerSessionPool matches an equal SNI", "[session_pool]")
{
  CHECK(ServerSessionPool::sni_matches("example.com", "example.com"));
}

TEST_CASE("ServerSessionPool refuses a different SNI", "[session_pool]")
{
  CHECK_FALSE(ServerSessionPool::sni_matches("example.com", "other.example.com"));
}

TEST_CASE("ServerSessionPool matches when neither side sends an SNI", "[session_pool]")
{
  // A next hop reached without an SNI -- an IP literal origin, for one -- would be reached the same
  // way again, so its sessions have to stay reusable. Refusing here retires every such session.
  CHECK(ServerSessionPool::sni_matches("", ""));
}

TEST_CASE("ServerSessionPool refuses a one-sided SNI", "[session_pool]")
{
  // Reuse in either direction would send a name the session was not established with, or drop one it
  // was.
  CHECK_FALSE(ServerSessionPool::sni_matches("example.com", ""));
  CHECK_FALSE(ServerSessionPool::sni_matches("", "example.com"));
}

TEST_CASE("ServerSessionPool treats a null session SNI as absent", "[session_pool]")
{
  // TLSSNISupport returns "" rather than nullptr, but the pool must not depend on that to stay safe.
  CHECK(ServerSessionPool::sni_matches("", nullptr));
  CHECK_FALSE(ServerSessionPool::sni_matches("example.com", nullptr));
}
