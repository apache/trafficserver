/** @file

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

#include "../P_SSLUtils.h"

#include "iocore/net/TLSBasicSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace
{
class TestTLSBasicSupport : public TLSBasicSupport
{
public:
  std::string offered_signature_algorithms;

protected:
  SSL *
  _get_ssl_object() const override
  {
    return reinterpret_cast<SSL *>(const_cast<TestTLSBasicSupport *>(this));
  }

  ssl_curve_id
  _get_tls_curve() const override
  {
    return 0;
  }

  std::string_view
  _get_tls_group() const override
  {
    return {};
  }

  std::string
  _get_tls_offered_signature_algorithms() const override
  {
    return offered_signature_algorithms;
  }

  std::string
  _get_tls_negotiated_signature_algorithm() const override
  {
    return {};
  }

  int
  _verify_certificate(X509_STORE_CTX *) override
  {
    return 0;
  }
};
} // namespace

TEST_CASE("TLS signature algorithms retain wire order and omit GREASE")
{
  std::array<uint16_t, 5> const algorithms{0x0403, 0x0a0a, 0x0804, 0x1a1a, 0xffff};

  CHECK(SSLFormatSignatureAlgorithms(algorithms) == "1027-2052-65535");
}

TEST_CASE("TLS signature algorithms containing only GREASE are empty")
{
  std::array<uint16_t, 3> const algorithms{0x0a0a, 0x5a5a, 0xfafa};

  CHECK(SSLFormatSignatureAlgorithms(algorithms).empty());
}

TEST_CASE("TLS signature algorithms preserve an earlier non-empty capture")
{
  TestTLSBasicSupport support;

  support.capture_tls_offered_signature_algorithms("1027-2052");
  support.capture_tls_offered_signature_algorithms();

  CHECK(support.get_tls_offered_signature_algorithms() == "1027-2052");
}
