/** @file

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

#include "ja4h.h"
#include "datasource.h"

#include <openssl/sha.h>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <map>

namespace
{

class MockDatasource : public Datasource
{
public:
  MockDatasource() {}
  ~MockDatasource() {}

  std::string_view
  get_method() override
  {
    return this->_method;
  }
  int
  get_version() override
  {
    return this->_version;
  }
  bool
  has_cookie_field() override
  {
    return this->_fields.contains("Cookie");
  }
  bool
  has_referer_field() override
  {
    return this->_fields.contains("Referer");
  }
  int
  get_field_count() override
  {
    return this->_fields.size();
  }
  std::string_view
  get_accept_language() override
  {
    if (auto ite = this->_fields.find("Accept-Language"); ite != this->_fields.end()) {
      return ite->second;
    } else {
      return {};
    }
  }
  void
  get_headers_hash(unsigned char out[32]) override
  {
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    for (auto ite : this->_fields) {
      if (this->_should_include_field({ite.first.c_str(), ite.first.size()})) {
        SHA256_Update(&sha256ctx, ite.first.c_str(), ite.first.size());
      }
    }

    SHA256_Final(out, &sha256ctx);
  }

  void
  set_method(std::string method)
  {
    this->_method = method;
  }
  void
  set_version(int version)
  {
    this->_version = version;
  }
  void
  set_fields(std::map<std::string, std::string> fields)
  {
    this->_fields = fields;
  }

private:
  std::string                        _method;
  int                                _version;
  std::map<std::string, std::string> _fields{};
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

TEST_CASE("JA4H")
{
  char fingerprint[ja4h::FINGERPRINT_LENGTH];

  SECTION("HTTP/1.0 GET with Host")
  {
    MockDatasource datasource;
    datasource.set_method("GET");
    datasource.set_version(1 << 16 | 0);
    datasource.set_fields({
      {"Host", "abc.example"},
    });

    ja4h::generate_fingerprint(fingerprint, datasource);

    std::string_view fingerprint_sv{fingerprint, sizeof(fingerprint)};
    CHECK(fingerprint_sv.substr(ja4h::PART_A_POSITION, ja4h::PART_A_LENGTH) == "ge10nn010000");
    CHECK(fingerprint_sv[ja4h::DELIMITER_1_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_B_POSITION, ja4h::PART_B_LENGTH) == SHA256_12("Host"));
    CHECK(fingerprint_sv[ja4h::DELIMITER_2_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_C_POSITION, ja4h::PART_C_LENGTH) == "000000000000");
    CHECK(fingerprint_sv[ja4h::DELIMITER_3_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_D_POSITION, ja4h::PART_D_LENGTH) == "000000000000");
  }

  SECTION("HTTP/1.1 POST with Accept-Language and Referer")
  {
    MockDatasource datasource;
    datasource.set_method("POST");
    datasource.set_version(1 << 16 | 1);
    datasource.set_fields({
      {"Accept-Language", "en"                     },
      {"Referer",         "https://xyz.example/foo"},
    });

    ja4h::generate_fingerprint(fingerprint, datasource);

    std::string_view fingerprint_sv{fingerprint, sizeof(fingerprint)};
    CHECK(fingerprint_sv.substr(ja4h::PART_A_POSITION, ja4h::PART_A_LENGTH) == "po11nr02en00");
    CHECK(fingerprint_sv[ja4h::DELIMITER_1_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_B_POSITION, ja4h::PART_B_LENGTH) == SHA256_12("Accept-Language"));
    CHECK(fingerprint_sv[ja4h::DELIMITER_2_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_C_POSITION, ja4h::PART_C_LENGTH) == "000000000000");
    CHECK(fingerprint_sv[ja4h::DELIMITER_3_POSITION] == ja4h::DELIMITER);
    CHECK(fingerprint_sv.substr(ja4h::PART_D_POSITION, ja4h::PART_D_LENGTH) == "000000000000");
  }
}
