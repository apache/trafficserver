/** @file ja3_fingerprint.cc
 *
  Unit tests for JA4 fingerprint calculation.

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

#include "ja4.h"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

static std::string call_JA4(JA4::TLSClientHelloSummary const &TLS_summary);
static std::string inc(std::string_view sv);

TEST_CASE("JA4")
{
  JA4::TLSClientHelloSummary TLS_summary;

  SECTION("Given the protocol is TCP, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 't'.")
  {
    TLS_summary.protocol = JA4::Protocol::TLS;

    CHECK("t" == call_JA4(TLS_summary).substr(0, 1));
  }

  SECTION("Given the protocol is QUIC, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 'q'.")
  {
    TLS_summary.protocol = JA4::Protocol::QUIC;
    CHECK(call_JA4(TLS_summary).starts_with('q'));
  }

  SECTION("Given the protocol is DTLS, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 'd'.")
  {
    TLS_summary.protocol = JA4::Protocol::DTLS;
    CHECK(call_JA4(TLS_summary).starts_with('d'));
  }

  SECTION("Given the TLS version is unknown, "
          "when we create a JA4 fingerprint, "
          "then indices [1,2] thereof should contain \"00\".")
  {
    TLS_summary.TLS_version = 0x123;
    CHECK("00" == call_JA4(TLS_summary).substr(1, 2));
    TLS_summary.TLS_version = 0x234;
    CHECK("00" == call_JA4(TLS_summary).substr(1, 2));
  }

  SECTION("Given the TLS version is known, "
          "when we create a JA4 fingerprint, "
          "then indices [1,2] thereof should contain the correct value.")
  {
    std::unordered_map<std::uint16_t, std::string> values{
      {0x304,  "13"},
      {0x303,  "12"},
      {0x302,  "11"},
      {0x301,  "10"},
      {0x300,  "s3"},
      {0x200,  "s2"},
      {0x100,  "s1"},
      {0xfeff, "d1"},
      {0xfefd, "d2"},
      {0xfefc, "d3"}
    };
    for (auto const &[version, expected] : values) {
      TLS_summary.TLS_version = version;
      CHECK(expected == call_JA4(TLS_summary).substr(1, 2));
    }
  }

  SECTION("Given the SNI is a domain name, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'd'.")
  {
    TLS_summary.SNI_type = JA4::SNI::to_domain;
    INFO(call_JA4(TLS_summary));
    CHECK("d" == call_JA4(TLS_summary).substr(3, 1));
  }

  SECTION("Given the SNI is an IP, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'i'.")
  {
    TLS_summary.SNI_type = JA4::SNI::to_IP;
    CHECK("i" == call_JA4(TLS_summary).substr(3, 1));
  }

  SECTION("Given there is one cipher, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"01\".")
  {
    TLS_summary.add_cipher(1);
    CHECK("01" == call_JA4(TLS_summary).substr(4, 2));
  }

  SECTION("Given there are 9 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"09\".")
  {
    for (int i{0}; i < 9; ++i) {
      TLS_summary.add_cipher(i);
    }
    CHECK("09" == call_JA4(TLS_summary).substr(4, 2));
  }

  SECTION("Given there are 10 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"10\".")
  {
    for (int i{0}; i < 10; ++i) {
      TLS_summary.add_cipher(i);
    }
    CHECK("10" == call_JA4(TLS_summary).substr(4, 2));
  }

  SECTION("Given there are more than 99 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"99\".")
  {
    for (int i{0}; i < 100; ++i) {
      TLS_summary.add_cipher(i);
    }
    CHECK("99" == call_JA4(TLS_summary).substr(4, 2));
  }

  SECTION("Given the ciphers include a GREASE value, "
          "when we create a JA4 fingerprint, "
          "then that value should not be included in the count.")
  {
    TLS_summary.add_cipher(0x0a0a);
    TLS_summary.add_cipher(72);
    CHECK("01" == call_JA4(TLS_summary).substr(4, 2));
  }

  SECTION("Given there are no extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"00\".")
  {
    CHECK("00" == call_JA4(TLS_summary).substr(6, 2));
  }

  SECTION("Given there are 9 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"09\".")
  {
    for (int i{0}; i < 9; ++i) {
      TLS_summary.add_extension(i);
    }
    CHECK("09" == call_JA4(TLS_summary).substr(6, 2));
  }

  SECTION("Given there are 99 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"99\".")
  {
    for (int i{0}; i < 99; ++i) {
      TLS_summary.add_extension(i);
    }
    CHECK("99" == call_JA4(TLS_summary).substr(6, 2));
  }

  SECTION("Given there are more than 99 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"99\".")
  {
    for (int i{0}; i < 100; ++i) {
      TLS_summary.add_extension(i);
    }
    CHECK("99" == call_JA4(TLS_summary).substr(6, 2));
  }

  SECTION("Given the extensions include a GREASE value, "
          "when we create a JA4 fingerprint, "
          "then that value should not be included in the count.")
  {
    TLS_summary.add_extension(2);
    TLS_summary.add_extension(0x0a0a);
    CHECK("01" == call_JA4(TLS_summary).substr(6, 2));
  }

  // These may be covered by the earlier tests as well, but this documents the
  // behavior explicitly.
  SECTION("When we create a JA4 fingerprint, "
          "then the SNI and ALPN extensions should be included in the count.")
  {
    TLS_summary.add_extension(0x0);
    TLS_summary.add_extension(0x10);
    CHECK("02" == call_JA4(TLS_summary).substr(6, 2));
  }

  SECTION("Given the ALPN value is empty, "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"00\".")
  {
    TLS_summary.ALPN = "";
    CHECK("00" == call_JA4(TLS_summary).substr(8, 2));
  }

  // This should never happen in practice because all registered ALPN values
  // are at least 2 characters long, but it's the correct behavior according
  // to the spec. :-)
  SECTION("Given the ALPN value is \"a\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"aa\".")
  {
    TLS_summary.ALPN = "a";
    CHECK("aa" == call_JA4(TLS_summary).substr(8, 2));
  }

  SECTION("Given the ALPN value is \"h3\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"h3\".")
  {
    TLS_summary.ALPN = "h3";
    CHECK("h3" == call_JA4(TLS_summary).substr(8, 2));
  }

  SECTION("Given the ALPN value is \"imap\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"ip\".")
  {
    TLS_summary.ALPN = "imap";
    CHECK("ip" == call_JA4(TLS_summary).substr(8, 2));
  }

  SECTION("When we create a JA4 fingeprint, "
          "then index 10 thereof should contain '_'.")
  {
    CHECK("_" == call_JA4(TLS_summary).substr(10, 1));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the b section should be passed through the hash function.")
  {
    TLS_summary.add_cipher(10);
    CHECK("111b" == JA4::make_JA4_fingerprint(TLS_summary, [](std::string_view sv) { return inc(sv); }).substr(11, 4));
  }

  // As per the spec, we expect 4-character, comma-delimited hex values.
  SECTION("Given only ciphers 2, 12, and 17 in that order, "
          "when we create a JA4 fingerprint, "
          "then the hash should be invoked with \"0002,000c,0011\".")
  {
    TLS_summary.add_cipher(2);
    TLS_summary.add_cipher(12);
    TLS_summary.add_cipher(17);
    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0002,000c,0011" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the cipher values should be sorted before hashing.")
  {
    TLS_summary.add_cipher(17);
    TLS_summary.add_cipher(2);
    TLS_summary.add_cipher(12);
    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0002,000c,0011" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  SECTION("When we create a JA4 fingerprint, "
          "then GREASE values in the cipher list should be ignored.")
  {
    TLS_summary.add_cipher(0x0a0a);
    TLS_summary.add_cipher(2);
    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0002" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  // All the tests from now on have enough ciphers to ensure a long enough
  // hash using our default hash (the id function) so that the length of the
  // JA4 fingerprint will be valid.
  TLS_summary.add_cipher(1);
  TLS_summary.add_cipher(2);
  TLS_summary.add_cipher(3);

  SECTION("When we create a JA4 fingerprint, "
          "then we should truncate the section b hash to 12 characters.")
  {
    CHECK("001,0002,000_" == JA4::make_JA4_fingerprint(TLS_summary, [](std::string_view sv) {
                               return sv.empty() ? sv : sv.substr(1);
                             }).substr(11, 13));
  }

  SECTION("When we create a JA4 fingeprint, "
          "then index 10 thereof should contain '_'.")
  {
    CHECK("_" == call_JA4(TLS_summary).substr(23, 1));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the c section should be passed through the hash function.")
  {
    TLS_summary.add_extension(10);
    CHECK("111b" == JA4::make_JA4_fingerprint(TLS_summary, [](std::string_view sv) { return inc(sv); }).substr(24, 4));
  }

  // As per the spec, we expect 4-character, comma-delimited hex values.
  SECTION("Given only extensions 2, 12, and 17 in that order, "
          "when we create a JA4 fingerprint, "
          "then the hash should be invoked with \"0002,000c,0011\".")
  {
    TLS_summary.add_extension(2);
    TLS_summary.add_extension(12);
    TLS_summary.add_extension(17);

    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0002,000c,0011" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the extension values should be sorted before hashing.")
  {
    TLS_summary.add_extension(17);
    TLS_summary.add_extension(2);
    TLS_summary.add_extension(12);
    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0002,000c,0011" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  SECTION("When we create a JA4 fingerprint, "
          "then we ignore GREASE, SNI, ALPN, and SNI values in the extensions.")
  {
    TLS_summary.add_extension(0x0a0a);
    TLS_summary.add_extension(0x0);
    TLS_summary.add_extension(0x10);
    TLS_summary.add_extension(5);
    bool verified{false};
    // INFO doesn't work from inside the lambda body. :/
    JA4::make_JA4_fingerprint(TLS_summary, [&verified](std::string_view sv) {
      if ("0005" == sv) {
        verified = true;
      }
      return sv;
    });
    CHECK(verified);
  }

  SECTION("When we create a JA4 fingerprint, "
          "then we total length of the fingerprint should be 36 characters.")
  {
    TLS_summary.add_extension(1);
    TLS_summary.add_extension(2);
    TLS_summary.add_extension(3);
    CHECK(36 == call_JA4(TLS_summary).size());
  }
}

std::string
call_JA4(JA4::TLSClientHelloSummary const &TLS_summary)
{
  return JA4::make_JA4_fingerprint(TLS_summary, [](std::string_view sv) { return sv; });
}

std::string
inc(std::string_view sv)
{
  std::string result;
  result.resize(sv.size());
  std::transform(sv.begin(), sv.end(), result.begin(), [](char c) { return c + 1; });
  return result;
}
