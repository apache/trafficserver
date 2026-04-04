/** @file
 *
  Unit tests for the JAWS v2 encoder.

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

#include "ja3/ja3_fingerprints.h"
#include "jaws_v2.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace
{
ja3::ClientHelloSummary
make_chrome_146_summary(bool include_grease)
{
  ja3::ClientHelloSummary summary{};
  summary.legacy_version        = 771;
  summary.effective_tls_version = 772;
  summary.ciphers    = include_grease ? std::vector<std::uint16_t>{0x1A1A, 0x1301, 0x1302, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC030,
                                                                   0xCCA9, 0xCCA8, 0xC013, 0xC014, 0x009C, 0x009D, 0x002F, 0x0035} :
                                        std::vector<std::uint16_t>{0x1301, 0x1302, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC030, 0xCCA9,
                                                                   0xCCA8, 0xC013, 0xC014, 0x009C, 0x009D, 0x002F, 0x0035};
  summary.extensions = include_grease ? std::vector<std::uint16_t>{0x0A0A, 0x0017, 0x0010, 0x0033, 0x002B, 0x000A, 0x0000, 0xFF01,
                                                                   0x0005, 0x002D, 0x0012, 0x001B, 0x0023, 0x000D, 0x000B} :
                                        std::vector<std::uint16_t>{0x0017, 0x0010, 0x0033, 0x002B, 0x000A, 0x0000, 0xFF01,
                                                                   0x0005, 0x002D, 0x0012, 0x001B, 0x0023, 0x000D, 0x000B};
  summary.curves     = include_grease ? std::vector<std::uint16_t>{0x4A4A, 0x11EC, 0x001D, 0x0017, 0x0018} :
                                        std::vector<std::uint16_t>{0x11EC, 0x001D, 0x0017, 0x0018};
  summary.point_formats          = {0x00};
  summary.ciphers_have_grease    = include_grease;
  summary.extensions_have_grease = include_grease;
  summary.curves_have_grease     = include_grease;
  return summary;
}
} // namespace

TEST_CASE("JAWS v2")
{
  SECTION("Chrome 146 keeps the legacy JA3 version while JAWS v2 uses the effective TLS version.")
  {
    auto const summary = make_chrome_146_summary(false);

    CHECK("771,4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,"
          "23-16-51-43-10-0-65281-5-45-18-27-35-13-11,4588-29-23-24,0" == ja3::make_ja3_raw(summary, false));
    CHECK("j2:772|15-fffe|14-bffe|4-2e|1-2" == ja3::jaws_v2::fingerprint(summary));
  }

  SECTION("JAWS v2 tracks per-section GREASE and can suppress the suffix without changing the payload.")
  {
    auto const summary = make_chrome_146_summary(true);

    CHECK("j2:772|15g-fffe|14g-bffe|4g-2e|1-2" == ja3::jaws_v2::fingerprint(summary));
    CHECK("j2:772|15-fffe|14-bffe|4-2e|1-2" == ja3::jaws_v2::fingerprint(summary, false));
  }

  SECTION("Legacy TLS 1.2 stacks and WinHTTP match the v2 vectors.")
  {
    ja3::ClientHelloSummary legacy{};
    legacy.legacy_version        = 771;
    legacy.effective_tls_version = 771;
    legacy.ciphers               = {0xC02F, 0xC030, 0xC02B, 0xC02C, 0x009C, 0x009D, 0x002F, 0x0035};
    legacy.extensions            = {0x0000, 0x000A, 0x000B, 0x000D, 0x0017, 0xFF01};
    legacy.curves                = {0x0017, 0x0018, 0x0019};
    legacy.point_formats         = {0x00, 0x01, 0x02};
    CHECK("j2:771|8-781e|6-13e|3-16|3-e" == ja3::jaws_v2::fingerprint(legacy));

    ja3::ClientHelloSummary winhttp{};
    winhttp.legacy_version        = 771;
    winhttp.effective_tls_version = 772;
    winhttp.ciphers               = {0x1302, 0x1301, 0xC02C, 0xC02B, 0xC030, 0xC02F};
    winhttp.extensions            = {0x0000, 0x000A, 0x000D, 0x002B, 0x0033, 0x002D};
    winhttp.curves                = {0x001D, 0x0017, 0x0018};
    CHECK("j2:772|6-19e|6-2ce|3-e|0-1" == ja3::jaws_v2::fingerprint(winhttp));
  }

  SECTION("Partial GREASE, duplicates, and unknown counts preserve their anomaly signal.")
  {
    ja3::ClientHelloSummary partial_grease{};
    partial_grease.legacy_version        = 771;
    partial_grease.effective_tls_version = 772;
    partial_grease.ciphers               = {0x2A2A, 0x1302, 0x1301, 0xC02C, 0xC02B, 0xC030, 0xC02F};
    partial_grease.extensions            = {0x0000, 0x000A, 0x000D, 0x002B, 0x0033, 0x002D};
    partial_grease.curves                = {0x001D, 0x0017, 0x0018};
    partial_grease.ciphers_have_grease   = true;
    CHECK("j2:772|6g-19e|6-2ce|3-e|0-1" == ja3::jaws_v2::fingerprint(partial_grease));

    ja3::ClientHelloSummary duplicates{};
    duplicates.legacy_version        = 771;
    duplicates.effective_tls_version = 771;
    duplicates.ciphers               = {0x002F, 0x002F, 0x0035};
    duplicates.point_formats         = {0x00};
    CHECK("j2:771|3-2800|0-0|0-0|1-2" == ja3::jaws_v2::fingerprint(duplicates));

    ja3::ClientHelloSummary unknowns{};
    unknowns.legacy_version        = 771;
    unknowns.effective_tls_version = 771;
    unknowns.ciphers               = {0x002F, 0x0035, 0x000A, 0xBEEF, 0xDEAD, 0xCAFE, 0xF00D, 0xBAD1};
    CHECK("j2:771|8-1002801|0-0|0-0|0-1" == ja3::jaws_v2::fingerprint(unknowns));
  }

  SECTION("GREASE-only sections remain empty but retain the grease tag.")
  {
    ja3::ClientHelloSummary summary{};
    summary.legacy_version        = 771;
    summary.effective_tls_version = 772;
    summary.ciphers               = {0x1A1A};
    summary.ciphers_have_grease   = true;
    CHECK("j2:772|0g-0|0-0|0-0|0-1" == ja3::jaws_v2::fingerprint(summary));
  }
}
