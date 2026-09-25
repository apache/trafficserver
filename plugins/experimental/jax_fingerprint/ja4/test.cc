/** @file
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
#include "datasource.h"

#include <catch2/catch_test_macros.hpp>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

class MockDatasource : public ja4::Datasource
{
public:
  std::string_view
  get_first_alpn() override
  {
    return this->_first_alpn;
  }

  void
  get_cipher_suites_hash(unsigned char out[32]) override
  {
    if (this->_ciphers.empty()) {
      memset(out, 0, 32);
      return;
    }
    auto sorted = this->_ciphers;
    std::sort(sorted.begin(), sorted.end());
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    for (size_t i = 0; i < sorted.size(); ++i) {
      char  buf[5];
      char *p = buf;
      if (i != 0) {
        *p  = ',';
        p  += 1;
      }
      uint16_t c   = sorted[i];
      uint8_t  h1  = (c & 0xF000) >> 12;
      uint8_t  l1  = (c & 0x0F00) >> 8;
      uint8_t  h2  = (c & 0x00F0) >> 4;
      uint8_t  l2  = c & 0x000F;
      p[0]         = h1 <= 9 ? ('0' + h1) : ('a' + h1 - 10);
      p[1]         = l1 <= 9 ? ('0' + l1) : ('a' + l1 - 10);
      p[2]         = h2 <= 9 ? ('0' + h2) : ('a' + h2 - 10);
      p[3]         = l2 <= 9 ? ('0' + l2) : ('a' + l2 - 10);
      p           += 4;
      SHA256_Update(&ctx, buf, p - buf);
    }
    SHA256_Final(out, &ctx);
  }

  void
  get_extension_hash(unsigned char out[32]) override
  {
    auto sorted = this->_extensions;
    std::sort(sorted.begin(), sorted.end());
    this->_hash_extensions(out, sorted.data(), static_cast<int>(sorted.size()),
                           this->_sig_algs.empty() ? nullptr : this->_sig_algs.data(), this->_sig_algs.size());
  }

  void
  set_protocol(ja4::Datasource::Protocol protocol)
  {
    this->_protocol = protocol;
  }
  void
  set_version(int version)
  {
    this->_version = version;
  }
  void
  set_first_alpn(std::string first_alpn)
  {
    this->_first_alpn = std::move(first_alpn);
  }
  void
  add_cipher(std::uint16_t cipher)
  {
    if (_is_GREASE(cipher)) {
      return;
    }

    ++this->_n_ciphers;
    this->_ciphers.push_back(cipher);
  }

  void
  add_extension(uint16_t extension)
  {
    if (EXT_SNI == extension) {
      this->_SNI_type = SNI::to_domain;
      this->_has_SNI  = true;
      return;
    }
    if (EXT_ALPN == extension) {
      this->_has_ALPN = true;
      return;
    }
    if (_is_GREASE(extension)) {
      return;
    }

    ++this->_n_extensions;
    this->_extensions.push_back(extension);
  }

  void
  set_signature_algorithms_extension(std::vector<unsigned char> body)
  {
    this->_sig_algs = std::move(body);
  }

private:
  std::string _first_alpn;

  std::vector<std::uint16_t> _ciphers;
  std::vector<std::uint16_t> _extensions;
  std::vector<unsigned char> _sig_algs;
  SNI                        _SNI_type{SNI::to_IP};
};

std::string_view
SHA256_12(std::string_view in)
{
  uint8_t hash[32];
  SHA256(reinterpret_cast<const uint8_t *>(in.data()), in.size(), hash);

  static char out[12];
  for (int i = 0; i < 6; ++i) {
    uint8_t h      = hash[i] >> 4;
    uint8_t l      = hash[i] & 0x0F;
    out[i * 2]     = h <= 9 ? '0' + h : 'a' + h - 10;
    out[i * 2 + 1] = l <= 9 ? '0' + l : 'a' + l - 10;
  }
  return {out, sizeof(out)};
}

} // namespace

static std::string call_JA4(ja4::Datasource &datasource);

TEST_CASE("JA4")
{
  MockDatasource datasource{};

  SECTION("Given the protocol is TLSP, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 't'.")
  {
    datasource.set_protocol(ja4::Datasource::Protocol::TLS);

    CHECK("t" == call_JA4(datasource).substr(0, 1));
  }

  SECTION("Given the protocol is QUIC, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 'q'.")
  {
    datasource.set_protocol(ja4::Datasource::Protocol::QUIC);
    CHECK(call_JA4(datasource).starts_with('q'));
  }

  SECTION("Given the protocol is DTLS, "
          "when we create a JA4 fingerprint, "
          "then the first character thereof should be 'd'.")
  {
    datasource.set_protocol(ja4::Datasource::Protocol::DTLS);
    CHECK(call_JA4(datasource).starts_with('d'));
  }

  SECTION("Given the TLS version is unknown, "
          "when we create a JA4 fingerprint, "
          "then indices [1,2] thereof should contain \"00\".")
  {
    datasource.set_version(0x123);
    CHECK("00" == call_JA4(datasource).substr(1, 2));
    datasource.set_version(0x234);
    CHECK("00" == call_JA4(datasource).substr(1, 2));
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
      datasource.set_version(version);
      CHECK(expected == call_JA4(datasource).substr(1, 2));
    }
  }

  SECTION("Given the SNI extension is present, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'd'.")
  {
    datasource.add_extension(0x0);
    CHECK("d" == call_JA4(datasource).substr(3, 1));
  }

  SECTION("Given the SNI extension is not present, "
          "when we create a JA4 fingerprint, "
          "then index 3 thereof should contain 'i'.")
  {
    datasource.add_extension(0x31);
    CHECK("i" == call_JA4(datasource).substr(3, 1));
  }

  SECTION("Given there is one cipher, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"01\".")
  {
    datasource.add_cipher(1);
    CHECK("01" == call_JA4(datasource).substr(4, 2));
  }

  SECTION("Given there are 9 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"09\".")
  {
    for (int i{0}; i < 9; ++i) {
      datasource.add_cipher(i);
    }
    CHECK("09" == call_JA4(datasource).substr(4, 2));
  }

  SECTION("Given there are 10 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"10\".")
  {
    for (int i{0}; i < 10; ++i) {
      datasource.add_cipher(i);
    }
    CHECK("10" == call_JA4(datasource).substr(4, 2));
  }

  SECTION("Given there are more than 99 ciphers, "
          "when we create a JA4 fingerprint, "
          "then indices [4,5] thereof should contain \"99\".")
  {
    for (int i{0}; i < 100; ++i) {
      datasource.add_cipher(i);
    }
    CHECK("99" == call_JA4(datasource).substr(4, 2));
  }

  SECTION("Given the ciphers include a GREASE value, "
          "when we create a JA4 fingerprint, "
          "then that value should not be included in the count.")
  {
    datasource.add_cipher(0x0a0a);
    datasource.add_cipher(72);
    CHECK("01" == call_JA4(datasource).substr(4, 2));
  }

  SECTION("Given there are no extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"00\".")
  {
    CHECK("00" == call_JA4(datasource).substr(6, 2));
  }

  SECTION("Given there are 9 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"09\".")
  {
    for (int i{0}; i < 9; ++i) {
      datasource.add_extension(i);
    }
    CHECK("09" == call_JA4(datasource).substr(6, 2));
  }

  SECTION("Given there are 99 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"99\".")
  {
    for (int i{0}; i < 99; ++i) {
      datasource.add_extension(i);
    }
    CHECK("99" == call_JA4(datasource).substr(6, 2));
  }

  SECTION("Given there are more than 99 extensions, "
          "when we create a JA4 fingerprint, "
          "then indices [6,7] thereof should contain \"99\".")
  {
    for (int i{0}; i < 100; ++i) {
      datasource.add_extension(i);
    }
    CHECK("99" == call_JA4(datasource).substr(6, 2));
  }

  SECTION("Given the extensions include a GREASE value, "
          "when we create a JA4 fingerprint, "
          "then that value should not be included in the count.")
  {
    datasource.add_extension(2);
    datasource.add_extension(0x0a0a);
    CHECK("01" == call_JA4(datasource).substr(6, 2));
  }

  // These may be covered by the earlier tests as well, but this documents the
  // behavior explicitly.
  SECTION("When we create a JA4 fingerprint, "
          "then the SNI and ALPN extensions should be included in the count.")
  {
    datasource.add_extension(0x0);
    datasource.add_extension(0x10);
    CHECK("02" == call_JA4(datasource).substr(6, 2));
  }

  SECTION("Given the ALPN value is empty, "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"00\".")
  {
    datasource.set_first_alpn("");
    CHECK("00" == call_JA4(datasource).substr(8, 2));
  }

  SECTION("Given the ALPN value is \"a\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"aa\".")
  {
    datasource.set_first_alpn("a");
    CHECK("aa" == call_JA4(datasource).substr(8, 2));
  }

  SECTION("Given the ALPN value is \"h3\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"h3\".")
  {
    datasource.set_first_alpn("h3");
    CHECK("h3" == call_JA4(datasource).substr(8, 2));
  }

  SECTION("Given the ALPN value is \"imap\", "
          "when we create a JA4 fingerprint, "
          "then indices [8,9] thereof should contain \"ip\".")
  {
    datasource.set_first_alpn("imap");
    CHECK("ip" == call_JA4(datasource).substr(8, 2));
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
    for (auto const &[alpn, expected] : values) {
      CAPTURE(alpn, expected);
      datasource.set_first_alpn(alpn);
      CHECK(expected == call_JA4(datasource).substr(8, 2));
    }
  }

  SECTION("When we create a JA4 fingerprint, "
          "then index 10 thereof should contain '_'.")
  {
    CHECK("_" == call_JA4(datasource).substr(10, 1));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the b section should be passed through the hash function.")
  {
    char buf[36];
    datasource.add_cipher(10);
    CHECK(SHA256_12("000a") == ja4::generate_fingerprint(buf, datasource).substr(11, 12));
  }

  // As per the spec, we expect 4-character, comma-delimited hex values.
  SECTION("Given only ciphers 2, 12, and 17 in that order, "
          "when we create a JA4 fingerprint, "
          "then the hash should be invoked with \"0002,000c,0011\".")
  {
    datasource.add_cipher(2);
    datasource.add_cipher(12);
    datasource.add_cipher(17);
    char buf[36];
    CHECK(SHA256_12("0002,000c,0011") == ja4::generate_fingerprint(buf, datasource).substr(11, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the cipher values should be sorted before hashing.")
  {
    datasource.add_cipher(17);
    datasource.add_cipher(2);
    datasource.add_cipher(12);
    char buf[36];
    CHECK(SHA256_12("0002,000c,0011") == ja4::generate_fingerprint(buf, datasource).substr(11, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then GREASE values in the cipher list should be ignored.")
  {
    datasource.add_cipher(0x0a0a);
    datasource.add_cipher(2);
    char buf[36];
    CHECK(SHA256_12("0002") == ja4::generate_fingerprint(buf, datasource).substr(11, 12));
  }

  // All the tests from now on have enough ciphers to ensure a long enough
  // hash using our default hash (the id function) so that the length of the
  // JA4 fingerprint will be valid.
  datasource.add_cipher(1);
  datasource.add_cipher(2);
  datasource.add_cipher(3);

  SECTION("When we create a JA4 fingerprint, "
          "then we should truncate the section b hash to 12 characters.")
  {
    char buf[36];
    CHECK(SHA256_12("0001,0002,0003") == ja4::generate_fingerprint(buf, datasource).substr(11, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then index 10 thereof should contain '_'.")
  {
    CHECK("_" == call_JA4(datasource).substr(23, 1));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the c section should be passed through the hash function.")
  {
    datasource.add_extension(10);
    char buf[36];
    CHECK(SHA256_12("000a") == ja4::generate_fingerprint(buf, datasource).substr(24, 12));
  }

  // As per the spec, we expect 4-character, comma-delimited hex values.
  SECTION("Given only extensions 2, 12, and 17 in that order, "
          "when we create a JA4 fingerprint, "
          "then the hash should be invoked with \"0002,000c,0011\".")
  {
    datasource.add_extension(2);
    datasource.add_extension(12);
    datasource.add_extension(17);

    char buf[36];
    CHECK(SHA256_12("0002,000c,0011") == ja4::generate_fingerprint(buf, datasource).substr(24, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then the extension values should be sorted before hashing.")
  {
    datasource.add_extension(17);
    datasource.add_extension(2);
    datasource.add_extension(12);

    char buf[36];
    CHECK(SHA256_12("0002,000c,0011") == ja4::generate_fingerprint(buf, datasource).substr(24, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then we ignore GREASE, SNI, ALPN, and SNI values in the extensions.")
  {
    datasource.add_extension(0x0a0a);
    datasource.add_extension(0x0);
    datasource.add_extension(0x10);
    datasource.add_extension(5);

    char buf[36];
    CHECK(SHA256_12("0005") == ja4::generate_fingerprint(buf, datasource).substr(24, 12));
  }

  SECTION("When we create a JA4 fingerprint, "
          "then we total length of the fingerprint should be 36 characters.")
  {
    datasource.add_extension(1);
    datasource.add_extension(2);
    datasource.add_extension(3);
    CHECK(36 == call_JA4(datasource).size());
  }
}

// The worked example from https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
TEST_CASE("JA4 specification example")
{
  MockDatasource datasource{};

  datasource.set_protocol(ja4::Datasource::Protocol::TLS);
  datasource.set_version(0x304);
  datasource.set_first_alpn("h2");
  for (std::uint16_t cipher :
       {0x1301, 0x1302, 0x1303, 0xc02b, 0xc02f, 0xc02c, 0xc030, 0xcca9, 0xcca8, 0xc013, 0xc014, 0x009c, 0x009d, 0x002f, 0x0035}) {
    datasource.add_cipher(cipher);
  }
  for (std::uint16_t extension : {0x001b, 0x0000, 0x0033, 0x0010, 0x4469, 0x0017, 0x002d, 0x000d, 0x0005, 0x0023, 0x0012, 0x002b,
                                  0xff01, 0x000b, 0x000a, 0x0015}) {
    datasource.add_extension(extension);
  }

  SECTION("Given the signature algorithms from the example, "
          "when we create a JA4 fingerprint, "
          "then it should match the one published in the specification.")
  {
    datasource.set_signature_algorithms_extension(
      {0x00, 0x10, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05, 0x03, 0x08, 0x05, 0x05, 0x01, 0x08, 0x06, 0x06, 0x01});
    CHECK("t13d1516h2_8daaf6152771_e5627efa2ab1" == call_JA4(datasource));
  }

  SECTION("Given GREASE values in the signature algorithms, "
          "when we create a JA4 fingerprint, "
          "then they should be ignored.")
  {
    datasource.set_signature_algorithms_extension({0x00, 0x14, 0x0a, 0x0a, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05,
                                                   0x03, 0x08, 0x05, 0x05, 0x01, 0x08, 0x06, 0x06, 0x01, 0xfa, 0xfa});
    CHECK("t13d1516h2_8daaf6152771_e5627efa2ab1" == call_JA4(datasource));
  }

  SECTION("Given no signature_algorithms extension, "
          "when we create a JA4 fingerprint, "
          "then the c section should match the one published in the specification.")
  {
    CHECK("6d807ffa2a79" == call_JA4(datasource).substr(24, 12));
  }

  SECTION("Given only GREASE values in the signature algorithms, "
          "when we create a JA4 fingerprint, "
          "then the c section should be hashed without the underscore.")
  {
    datasource.set_signature_algorithms_extension({0x00, 0x02, 0x0a, 0x0a});
    CHECK("6d807ffa2a79" == call_JA4(datasource).substr(24, 12));
  }

  SECTION("Given a signature algorithms list shorter than the extension, "
          "when we create a JA4 fingerprint, "
          "then only the algorithms within the declared length should be hashed.")
  {
    datasource.set_signature_algorithms_extension({0x00, 0x02, 0x04, 0x03, 0x08, 0x04});
    CHECK(SHA256_12("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403") ==
          call_JA4(datasource).substr(24, 12));
  }

  SECTION("Given a signature algorithms list longer than the extension, "
          "when we create a JA4 fingerprint, "
          "then only the algorithms present should be hashed.")
  {
    datasource.set_signature_algorithms_extension({0x00, 0x10, 0x04, 0x03, 0x08});
    CHECK(SHA256_12("0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403") ==
          call_JA4(datasource).substr(24, 12));
  }
}

std::string
call_JA4(ja4::Datasource &datasource)
{
  char buf[36];
  ja4::generate_fingerprint(buf, datasource);
  return {buf, 36};
}
