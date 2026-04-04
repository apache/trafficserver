/** @file
 *
  Unit tests for the frozen JAWS v1 encoder.

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

#include "jaws.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

using namespace std::literals;

namespace
{
constexpr std::string_view full_JA3_sample{"771,4866-4867-4865-49196-49200-"
                                           "159-52393-52392-52394-49195-49199-158-49188-49192-107-49187-49191-103-"
                                           "49162-49172-57-49161-49171-51-157-156-61-60-53-47-255,0-11-10-35-13172-"
                                           "16-22-23-13-43-45-51,29-23-30-25-24-256-257-258-259-260,0-1-2"};
} // end anonymous namespace

TEST_CASE("JAWS v1")
{
  SECTION("When we fingerprint a parsed summary, then we should get the frozen JAWS v1 output.")
  {
    ja3::ClientHelloSummary summary{};
    summary.legacy_version        = 771;
    summary.effective_tls_version = 772;
    summary.ciphers               = {0x1301, 0x1302, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC030, 0xCCA9,
                                     0xCCA8, 0xC013, 0xC014, 0x009C, 0x009D, 0x002F, 0x0035};
    summary.extensions            = {0x0017, 0x0010, 0x0033, 0x002B, 0x000A, 0x0000, 0xFF01,
                                     0x0005, 0x002D, 0x0012, 0x001B, 0x0023, 0x000D, 0x000B};
    summary.curves                = {0x11EC, 0x001D, 0x0017, 0x0018};
    summary.point_formats         = {0x00};

    CHECK("15-26307fe|14-1dc040414682|4-17" == ja3::jaws_v1::fingerprint(summary));
  }

  SECTION("When the input JA3 string is empty, then we don't crash.")
  {
    CHECK_NOTHROW(ja3::jaws_v1::score(""));
  }

  SECTION("When the input JA3 string is missing parts, then we don't crash.")
  {
    CHECK_NOTHROW(ja3::jaws_v1::score(",,,"));
  }

  SECTION("When the input JA3 string has too many parts, then we don't crash.")
  {
    CHECK_NOTHROW(ja3::jaws_v1::score(",,,,,,,,"sv));
  }

  SECTION("When the input JA3 string has delimiters in bad places, then we don't crash.")
  {
    CHECK_NOTHROW(ja3::jaws_v1::score("yay-woohoo-oof"));
  }

  SECTION("When the input JA3 string is completely bogus but well formatted, then we get lots of anomalies.")
  {
    CHECK("1-1|1-1|1-1" == ja3::jaws_v1::score("771,haha,this,ain't\t, good!!&#$"));
  }

  SECTION("When we score a JA3 string with an anomalous cipher, then we set the anomaly bit in the ciphers score.")
  {
    CHECK("1-1|0-0|0-0" == ja3::jaws_v1::score("771,70,,,"));
  }

  SECTION("When we pass a string_view, then it should still be happy.")
  {
    CHECK("1-1|0-0|0-0" == ja3::jaws_v1::score("771,70,,,"sv));
  }

  SECTION("When we score a JA3 string with a single cipher, then we should get the correct encoding.")
  {
    CHECK("1-2|0-0|0-0" == ja3::jaws_v1::score("771,49172,,,"));
    CHECK("1-40000000|0-0|0-0" == ja3::jaws_v1::score("771,57,,,"));
  }

  SECTION("When we score a JA3 string with two ciphers, then the encodings should be bitwise or'd.")
  {
    CHECK("2-2200000|0-0|0-0" == ja3::jaws_v1::score("771,4866-4867,,,"));
  }

  SECTION("When we encode a cipher with a high anchor index, then it better not overflow.")
  {
    CHECK("1-200000000000000000000000000000000|0-0|0-0" == ja3::jaws_v1::score("771,129,,,"));
  }

  SECTION("When we score a JA3 string with an anomalous extension, then we set the anomaly bit in the extensions score.")
  {
    CHECK("1-2|1-1|0-0" == ja3::jaws_v1::score("771,49172,70,,"));
  }

  SECTION("When we score a JA3 string with a single extension, then we should get the correct encoding.")
  {
    CHECK("1-2|1-4|0-0" == ja3::jaws_v1::score("771,49172,24,,"));
    CHECK("1-2|1-20000|0-0" == ja3::jaws_v1::score("771,49172,17,,"));
  }

  SECTION("When we score a JA3 string with two extensions, then the encodings should be bitwise or'd.")
  {
    CHECK("1-2|2-80002|0-0" == ja3::jaws_v1::score("771,49172,8-23,,"));
  }

  SECTION("When we encode an extension with a high anchor index, then it better not overflow.")
  {
    CHECK("1-2|1-200000000000|0-0" == ja3::jaws_v1::score("771,49172,41,,"));
  }

  SECTION("When we score a JA3 string with an anomalous elliptic curve, then we set the anomaly bit in the elliptic curve score.")
  {
    CHECK("1-2|1-400|1-1" == ja3::jaws_v1::score("771,49172,10,70,"));
  }

  SECTION("When we score a JA3 string with a single elliptic curve, then we should get the correct encoding.")
  {
    CHECK("1-2|1-400|1-10" == ja3::jaws_v1::score("771,49172,10,29,"));
  }

  SECTION("When we score a JA3 string with two elliptic curves, then the encodings should be bitwise or'd.")
  {
    CHECK("1-2|1-400|2-180" == ja3::jaws_v1::score("771,49172,10,9-14,"));
  }

  SECTION("When we encode an elliptic curve with a high anchor index, then it better not overflow.")
  {
    CHECK("1-2|1-400|1-1000000000" == ja3::jaws_v1::score("771,49172,10,16696,"));
  }

  SECTION("When we score a real JA3 string, then we should get the same result as the frozen implementation.")
  {
    CHECK("31-1ce3fffffe|12-1cc000010693|10-e0000605e" == ja3::jaws_v1::score(full_JA3_sample));
  }
}
