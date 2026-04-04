/** @file test_utils.cc

  Unit tests for ja3.

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

#include "ja3_utils.h"
#include "ja3_fingerprints.h"

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
} // end anonymous namespace

TEST_CASE("ja3 byte buffer encoding")
{
  unsigned char const buf[]{0x8, 0x3, 0x4};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_byte_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_byte_buffer(buf, 1)};
    CHECK("8" == got);
  }

  SECTION("3 values")
  {
    auto got{ja3::encode_byte_buffer(buf, 3)};
    CHECK("8-3-4" == got);
  }
}

TEST_CASE("ja3 word buffer encoding")
{
  unsigned char const buf[]{0x0, 0x5, 0x0a, 0x0a, 0x0, 0x8, 0xda, 0xda, 0x1, 0x0};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_word_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_word_buffer(buf, 2)};
    CHECK("5" == got);
  }

  SECTION("5 values including GREASE values")
  {
    auto got{ja3::encode_word_buffer(buf, 10)};
    CHECK("5-8-256" == got);
  }
}

TEST_CASE("ja3 integer buffer encoding")
{
  int const buf[]{5, 2570, 8, 56026, 256};

  SECTION("empty buffer")
  {
    auto got{ja3::encode_integer_buffer(nullptr, 0)};
    CHECK("" == got);
  }

  SECTION("1 value")
  {
    auto got{ja3::encode_integer_buffer(buf, 1)};
    CHECK("5" == got);
  }

  SECTION("5 values including GREASE values")
  {
    auto got{ja3::encode_integer_buffer(buf, 5)};
    CHECK("5-8-256" == got);
  }
}

TEST_CASE("JA3 fingerprints")
{
  SECTION("Chrome 146 preserves the historical JA3 raw and hash output.")
  {
    auto const summary = make_chrome_146_summary(false);

    CHECK("771,4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,"
          "23-16-51-43-10-0-65281-5-45-18-27-35-13-11,4588-29-23-24,0" == ja3::make_ja3_raw(summary, false));
    CHECK("7967c29a47449c6939cfa8b5b68fe55f" == ja3::make_ja3_hash(summary));
  }

  SECTION("JA3 raw grease-preserved encoding retains GREASE values.")
  {
    auto const summary = make_chrome_146_summary(true);

    CHECK("771,6682-4865-4866-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,"
          "2570-23-16-51-43-10-0-65281-5-45-18-27-35-13-11,19018-4588-29-23-24,0" == ja3::make_ja3_raw(summary, true));
  }
}
