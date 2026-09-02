/** @file

  Tests for QUIC token key configuration.

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

#include "iocore/net/quic/QUICConfig.h"
#include "iocore/net/quic/QUICTypes.h"
#include "records/RecCore.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
class TokenKeyFile
{
public:
  explicit TokenKeyFile(const std::string &contents)
    : _path(std::filesystem::temp_directory_path() / ("ats-quic-token-key-" + std::to_string(getpid())))
  {
    write(contents);
  }

  ~TokenKeyFile()
  {
    std::error_code ec;
    std::filesystem::remove(_path, ec);
  }

  void
  write(const std::string &contents) const
  {
    std::ofstream output(_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(contents.data(), contents.size());
    REQUIRE(output.good());
  }

  std::string
  path() const
  {
    return _path.string();
  }

private:
  std::filesystem::path _path;
};
} // namespace

TEST_CASE("QUIC tokens use reloadable key files", "[quic][security]")
{
  std::string const key_a(QUICTokenKeyConfigParams::KEY_LENGTH, 'A');
  std::string const key_b(QUICTokenKeyConfigParams::KEY_LENGTH, 'B');
  TokenKeyFile      key_file(key_a + key_b);

  REQUIRE(RecSetRecordString("proxy.config.quic.server.token_key.filename", key_file.path().c_str(), REC_SOURCE_EXPLICIT) ==
          REC_ERR_OKAY);
  REQUIRE(QUICTokenKeyConfig::reconfigure());

  IpEndpoint source;
  IpEndpoint other_source;
  REQUIRE(ats_ip_pton("192.0.2.1:443", &source.sa) == 0);
  REQUIRE(ats_ip_pton("192.0.2.2:443", &other_source.sa) == 0);

  uint8_t const    original_dcid_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  uint8_t const    scid_data[]          = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  QUICConnectionId original_dcid(original_dcid_data, sizeof(original_dcid_data));
  QUICConnectionId scid(scid_data, sizeof(scid_data));
  ink_hrtime const expire_time = ink_get_hrtime() + HRTIME_SECONDS(60);

  QUICRetryToken          retry_with_a(source, original_dcid, scid);
  QUICResumptionToken     resumption_with_a(source, scid, expire_time);
  QUICStatelessResetToken reset_with_a(scid, 1);
  CHECK(retry_with_a.is_valid(source));
  CHECK_FALSE(retry_with_a.is_valid(other_source));
  CHECK(resumption_with_a.is_valid(source));
  CHECK_FALSE(resumption_with_a.is_valid(other_source));

  uint8_t const       malformed_data[] = {static_cast<uint8_t>(QUICAddressValidationToken::Type::RETRY)};
  QUICRetryToken      malformed_retry(malformed_data, sizeof(malformed_data));
  QUICResumptionToken malformed_resumption(malformed_data, sizeof(malformed_data));
  CHECK_FALSE(malformed_retry.is_valid(source));
  CHECK_FALSE(malformed_resumption.is_valid(source));

  key_file.write(key_b + key_a);
  REQUIRE(QUICTokenKeyConfig::reconfigure());

  QUICRetryToken          retry_with_b(source, original_dcid, scid);
  QUICResumptionToken     resumption_with_b(source, scid, expire_time);
  QUICStatelessResetToken reset_with_b(scid, 1);
  CHECK(retry_with_a.is_valid(source));
  CHECK(resumption_with_a.is_valid(source));
  CHECK(retry_with_a != retry_with_b);
  CHECK(resumption_with_a != resumption_with_b);
  CHECK(reset_with_a != reset_with_b);

  key_file.write(key_b);
  REQUIRE(QUICTokenKeyConfig::reconfigure());
  CHECK_FALSE(retry_with_a.is_valid(source));
  CHECK_FALSE(resumption_with_a.is_valid(source));
  CHECK(retry_with_b.is_valid(source));
  CHECK(resumption_with_b.is_valid(source));

  key_file.write(std::string(QUICTokenKeyConfigParams::KEY_LENGTH - 1, 'C'));
  CHECK_FALSE(QUICTokenKeyConfig::reconfigure());
  CHECK(retry_with_b.is_valid(source));

  REQUIRE(RecSetRecordString("proxy.config.quic.server.token_key.filename", "", REC_SOURCE_EXPLICIT) == REC_ERR_OKAY);
  REQUIRE(QUICTokenKeyConfig::reconfigure());
  CHECK_FALSE(retry_with_b.is_valid(source));

  QUICRetryToken random_retry(source, original_dcid, scid);
  REQUIRE(QUICTokenKeyConfig::reconfigure());
  CHECK(random_retry.is_valid(source));
  CHECK(random_retry == QUICRetryToken(source, original_dcid, scid));
}
