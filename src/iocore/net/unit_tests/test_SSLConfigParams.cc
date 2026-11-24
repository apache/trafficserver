/** @file

  Catch based unit tests for SSLConfigParams construction

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

#include "../P_SSLConfig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <new>

TEST_CASE("SSLConfigParams is usable before initialize()", "[net][SSLConfigParams]")
{
  // Unit tests build SSLConfigParams directly and hand it to the context loaders without calling
  // initialize(). Construct over a fill pattern so a field the constructor skips cannot pass by
  // happening to be zero.
  alignas(SSLConfigParams) unsigned char storage[sizeof(SSLConfigParams)];
  std::memset(storage, 0xA5, sizeof(storage));
  auto *params = new (storage) SSLConfigParams;

  CHECK(params->server_tls_ver_min == -1);
  CHECK(params->server_tls_ver_max == -1);
  CHECK(params->client_tls_ver_min == -1);
  CHECK(params->client_tls_ver_max == -1);
  CHECK(params->clientCertExitOnLoadError == 0);
  CHECK(params->alpn_protocols_array_size == 0);
  CHECK(std::all_of(std::begin(params->alpn_protocols_array), std::end(params->alpn_protocols_array),
                    [](unsigned char c) { return c == 0; }));

  params->~SSLConfigParams();
}
