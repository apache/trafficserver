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

#include "ja4h.h"
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>

static void
generate_ja4h_a(char *out, Datasource &datasource)
{
  std::string_view method = datasource.get_method();
  if (method.length() >= 2) {
    out[0] = std::tolower(method[0]);
    out[1] = std::tolower(method[1]);
  } else {
    // This case seems to be undefined on the spec
    out[0] = 'x';
    out[1] = 'x';
  }
  int version     = datasource.get_version();
  out[2]          = 0x30 | (version >> 16);
  out[3]          = 0x30 | (version & 0xFFFF);
  out[4]          = datasource.has_cookie_field() ? 'c' : 'n';
  out[5]          = datasource.has_referer_field() ? 'r' : 'n';
  int field_count = datasource.get_field_count();
  if (field_count < 100) {
    out[6] = 0x30 | (field_count / 10);
    out[7] = 0x30 | (field_count % 10);
  } else {
    out[6] = '9';
    out[7] = '9';
  }
  std::string_view accept_lang = datasource.get_accept_language();
  if (accept_lang.empty()) {
    out[8]  = '0';
    out[9]  = '0';
    out[10] = '0';
    out[11] = '0';
  } else {
    for (int i = 0, j = 0; i < 4; ++i) {
      while (static_cast<size_t>(j) < accept_lang.size() && accept_lang[j] < 'A' && accept_lang[j] != ';') {
        ++j;
      }
      if (static_cast<size_t>(j) == accept_lang.size() || accept_lang[j] == ';') {
        out[8 + i] = '0';
      } else {
        out[8 + i] = std::tolower(accept_lang[j]);
        ++j;
      }
    }
  }
}

static void
generate_ja4h_b(char *out, Datasource &datasource)
{
  unsigned char hash[32];

  datasource.get_headers_hash(hash);

  for (int i = 0; i < 6; ++i) {
    unsigned int h = hash[i] >> 4;
    unsigned int l = hash[i] & 0x0F;
    out[i * 2]     = h <= 9 ? ('0' + h) : ('a' + h - 10);
    out[i * 2 + 1] = l <= 9 ? ('0' + l) : ('a' + l - 10);
  }
}

static void
generate_ja4h_c(char *out, Datasource & /* datasource ATS_UNUSED */)
{
  // Not implemented
  for (int i = 0; i < 12; ++i) {
    out[i] = '0';
  }
}

static void
generate_ja4h_d(char *out, Datasource & /* datasource ATS_UNUSED */)
{
  // Not implemented
  for (int i = 0; i < 12; ++i) {
    out[i] = '0';
  }
}

void
ja4h::generate_fingerprint(char *out, Datasource &datasource)
{
  generate_ja4h_a(&(out[PART_A_POSITION]), datasource);
  out[DELIMITER_1_POSITION] = DELIMITER;
  generate_ja4h_b(&(out[PART_B_POSITION]), datasource);
  out[DELIMITER_2_POSITION] = DELIMITER;
  generate_ja4h_c(&(out[PART_C_POSITION]), datasource);
  out[DELIMITER_3_POSITION] = DELIMITER;
  generate_ja4h_d(&(out[PART_D_POSITION]), datasource);
}
