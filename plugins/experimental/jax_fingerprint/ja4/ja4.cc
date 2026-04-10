/** @file
 *

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
#include <common/utils.h>

#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>
#include <string>
#include <string_view>

static void convert_protocol_to_char(char *out, ja4::Datasource::Protocol protocol);
static void convert_TLS_version_to_string(char *out, std::uint16_t TLS_version);
static void convert_SNI_to_char(char *out, ja4::Datasource::SNI SNI_type);
static void convert_count_to_two_digit_string(char *out, std::size_t count);
static void convert_ALPN_to_two_char_string(char *out, std::string_view ALPN);

/**
 * Calculate the a portion of the JA4 fingerprint for the given client hello.
 *
 * The a portion of the fingerprint encodes the protocol, TLS version, SNI
 * type, number of cipher suites, number of extensions, and first ALPN value.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the a portion of the JA4 fingerprint.
 */
static void
make_JA4_a(char *out, ja4::Datasource &datasource)
{
  convert_protocol_to_char(out, datasource.get_protocol());
  out += 1;

  convert_TLS_version_to_string(out, datasource.get_version());
  out += 2;

  convert_SNI_to_char(out, datasource.get_sni_type());
  out += 1;

  convert_count_to_two_digit_string(out, datasource.get_cipher_count());
  out += 2;

  convert_count_to_two_digit_string(out, datasource.get_extension_count());
  out += 2;

  convert_ALPN_to_two_char_string(out, datasource.get_first_alpn());
}

static void
convert_protocol_to_char(char *out, ja4::Datasource::Protocol protocol)
{
  out[0] = static_cast<char>(protocol);
}

static void
convert_TLS_version_to_string(char *out, std::uint16_t version)
{
  switch (version) {
  case 0x304:
    out[0] = '1';
    out[1] = '3';
    break;
  case 0x303:
    out[0] = '1';
    out[1] = '2';
    break;
  case 0x302:
    out[0] = '1';
    out[1] = '1';
    break;
  case 0x301:
    out[0] = '1';
    out[1] = '0';
    break;
  case 0x300:
    out[0] = 's';
    out[1] = '3';
    break;
  case 0x200:
    out[0] = 's';
    out[1] = '2';
    break;
  case 0x100:
    out[0] = 's';
    out[1] = '1';
    break;
  case 0xfeff:
    out[0] = 'd';
    out[1] = '1';
    break;
  case 0xfefd:
    out[0] = 'd';
    out[1] = '2';
    break;
  case 0xfefc:
    out[0] = 'd';
    out[1] = '3';
    break;
  default:
    out[0] = '0';
    out[1] = '0';
    break;
  }
}

static void
convert_SNI_to_char(char *out, ja4::Datasource::SNI type)
{
  out[0] = static_cast<char>(type);
}

static void
convert_count_to_two_digit_string(char *out, std::size_t count)
{
  if (count <= 99) {
    out[0] = (count / 10) + '0';
    out[1] = (count % 10) + '0';
  } else {
    out[0] = '9';
    out[1] = '9';
  }
}

static void
convert_ALPN_to_two_char_string(char *out, std::string_view alpn)
{
  if (alpn.empty()) {
    out[0] = '0';
    out[1] = '0';
  } else {
    out[0] = alpn.front();
    out[1] = alpn.back();
  }
}

/**
 * Calculate the b portion of the JA4 fingerprint for the given client hello.
 *
 * The b portion of the fingerprint is a comma-delimited list of lowercase hex
 * numbers representing the cipher suites in sorted order. GREASE values are
 * ignored.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the b portion of the JA4 fingerprint.
 */
static void
make_JA4_b(char *out, ja4::Datasource &datasource)
{
  unsigned char hash[32];

  datasource.get_cipher_suites_hash(hash);
  hash_stringify(out, hash);
}

/**
 * Calculate the c portion of the JA4 fingerprint for the given client hello.
 *
 * The b portion of the fingerprint is a comma-delimited list of lowercase hex
 * numbers representing the extensions in sorted order. GREASE values and the
 * SNI and ALPN extensions are ignored.
 *
 * For more information see:
 * https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md.
 *
 * @param TLS_summary The TLS client hello.
 * @return Returns a string containing the c portion of the JA4 fingerprint.
 */
static void
make_JA4_c(char *out, ja4::Datasource &datasource)
{
  unsigned char hash[32];

  datasource.get_extension_hash(hash);
  hash_stringify(out, hash);
}

std::string_view
ja4::generate_fingerprint(char *out, ja4::Datasource &datasource)
{
  make_JA4_a(&(out[ja4::PART_A_POSITION]), datasource);
  out[ja4::DELIMITER_1_POSITION] = ja4::PORTION_DELIMITER;
  make_JA4_b(&(out[ja4::PART_B_POSITION]), datasource);
  out[ja4::DELIMITER_2_POSITION] = ja4::PORTION_DELIMITER;
  make_JA4_c(&(out[ja4::PART_C_POSITION]), datasource);

  return {out, FINGERPRINT_LENGTH};
}
