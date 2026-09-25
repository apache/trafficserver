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

#include <catch2/catch_test_macros.hpp>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

static std::string call_JA4(JA4::TLSClientHelloSummary const &TLS_summary);
static std::string inc(std::string_view sv);
static std::string sha256(std::string_view sv);

TEST_CASE("JA4")
{
  JA4::TLSClientHelloSummary TLS_summary{};

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
      CAPTURE(version, expected);
      TLS_summary.TLS_version = version;
      CHECK(expected == call_JA4(TLS_summary).substr(1, 2));
    }
  }

  SECTION("Given the SNI extension is present, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'd'.")
  {
    TLS_summary.add_extension(0x0);
    CHECK("d" == call_JA4(TLS_summary).substr(3, 1));
  }

  SECTION("Given the SNI extension is not present, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'i'.")
  {
    TLS_summary.add_extension(0x31);
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

  SECTION("Given the ALPN value is \"a\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"aa\".")
  {
    TLS_summary.ALPN = 'a';
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

  SECTION("Given the first or the last byte of the ALPN value is not ASCII alphanumeric, "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain the hex representation of those bytes.")
  {
    std::vector<std::pair<std::string, std::string>> values{
      {std::string{"\xab"},             "ab"},
      {std::string{"\x20"},             "20"},
      {std::string{"\xab\xcd"},         "ad"},
      {std::string{"\x20\x61"},         "21"},
      {std::string{"\x30\xab"},         "3b"},
      {std::string{"\x61\x20"},         "60"},
      {std::string{"\x0a\x0a"},         "0a"},
      {std::string{"\x30\x31\xab\xcd"}, "3d"},
      {std::string{"\x30\xab\xcd\x31"}, "01"}
    };
    for (auto const &[ALPN, expected] : values) {
      CAPTURE(ALPN, expected);
      TLS_summary.ALPN = ALPN;
      CHECK(expected == call_JA4(TLS_summary).substr(8, 2));
    }
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

// The worked example from https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
TEST_CASE("JA4 specification example")
{
  JA4::TLSClientHelloSummary TLS_summary{};

  TLS_summary.protocol    = JA4::Protocol::TLS;
  TLS_summary.TLS_version = 0x304;
  TLS_summary.ALPN        = "h2";
  for (std::uint16_t cipher :
       {0x1301, 0x1302, 0x1303, 0xc02b, 0xc02f, 0xc02c, 0xc030, 0xcca9, 0xcca8, 0xc013, 0xc014, 0x009c, 0x009d, 0x002f, 0x0035}) {
    TLS_summary.add_cipher(cipher);
  }
  for (std::uint16_t extension : {0x001b, 0x0000, 0x0033, 0x0010, 0x4469, 0x0017, 0x002d, 0x000d, 0x0005, 0x0023, 0x0012, 0x002b,
                                  0xff01, 0x000b, 0x000a, 0x0015}) {
    TLS_summary.add_extension(extension);
  }

  SECTION("Given the signature algorithms from the example, "
          "when we create a JA4 fingerprint, "
          "then it should match the one published in the specification.")
  {
    unsigned char const sig_algs[]{0x00, 0x10, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05,
                                   0x03, 0x08, 0x05, 0x05, 0x01, 0x08, 0x06, 0x06, 0x01};
    TLS_summary.set_signature_algorithms(sig_algs, sizeof(sig_algs));
    CHECK("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403,0804,0401,0503,0805,0501,0806,0601" ==
          JA4::make_JA4_c_raw(TLS_summary));
    CHECK("t13d1516h2_8daaf6152771_e5627efa2ab1" == JA4::make_JA4_fingerprint(TLS_summary, sha256));
  }

  SECTION("Given GREASE values in the signature algorithms, "
          "when we create a JA4 fingerprint, "
          "then they should be ignored.")
  {
    unsigned char const sig_algs[]{0x00, 0x14, 0x0a, 0x0a, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05,
                                   0x03, 0x08, 0x05, 0x05, 0x01, 0x08, 0x06, 0x06, 0x01, 0xfa, 0xfa};
    TLS_summary.set_signature_algorithms(sig_algs, sizeof(sig_algs));
    CHECK("t13d1516h2_8daaf6152771_e5627efa2ab1" == JA4::make_JA4_fingerprint(TLS_summary, sha256));
  }

  SECTION("Given no signature_algorithms extension, "
          "when we create a JA4 fingerprint, "
          "then the c section should match the one published in the specification.")
  {
    CHECK("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01" == JA4::make_JA4_c_raw(TLS_summary));
    CHECK("6d807ffa2a79" == JA4::make_JA4_fingerprint(TLS_summary, sha256).substr(24, 12));
  }

  SECTION("Given only GREASE values in the signature algorithms, "
          "when we create a JA4 fingerprint, "
          "then the c section should end without an underscore.")
  {
    unsigned char const sig_algs[]{0x00, 0x02, 0x0a, 0x0a};
    TLS_summary.set_signature_algorithms(sig_algs, sizeof(sig_algs));
    CHECK("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01" == JA4::make_JA4_c_raw(TLS_summary));
  }

  SECTION("Given a signature algorithms list shorter than the extension, "
          "when we create a JA4 fingerprint, "
          "then only the algorithms within the declared length should be used.")
  {
    unsigned char const sig_algs[]{0x00, 0x02, 0x04, 0x03, 0x08, 0x04};
    TLS_summary.set_signature_algorithms(sig_algs, sizeof(sig_algs));
    CHECK("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403" == JA4::make_JA4_c_raw(TLS_summary));
  }

  SECTION("Given a signature algorithms list longer than the extension, "
          "when we create a JA4 fingerprint, "
          "then only the algorithms present should be used.")
  {
    unsigned char const sig_algs[]{0x00, 0x10, 0x04, 0x03, 0x08};
    TLS_summary.set_signature_algorithms(sig_algs, sizeof(sig_algs));
    CHECK("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403" == JA4::make_JA4_c_raw(TLS_summary));
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

std::string
sha256(std::string_view sv)
{
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<unsigned char const *>(sv.data()), sv.size(), hash);
  std::string result(SHA256_DIGEST_LENGTH * 2, '\0');
  for (int i{0}; i < SHA256_DIGEST_LENGTH; ++i) {
    std::snprintf(result.data() + (i * 2), 3, "%02x", hash[i]);
  }
  return result;
}
