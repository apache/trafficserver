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

#include "ts/ts.h"

#include "../plugin.h"
#include "../context.h"
#include "ja4h_method.h"
#include "ja4h.h"

namespace ja4h_method
{
void on_request(JAxContext *, TSHttpTxn);

struct Method method = {
  "JA4H",
  Method::Type::REQUEST_BASED,
  nullptr,
  on_request,
};

} // namespace ja4h_method

constexpr int JA4H_FINGERPRINT_LENGTH = 51;

static std::string
get_fingerprint(TSHttpTxn txnp)
{
  char      fingerprint[JA4H_FINGERPRINT_LENGTH];
  Extractor ex(txnp);
  // JA4H_a
  std::string_view method = ex.get_method();
  if (method.length() >= 2) {
    fingerprint[0] = std::tolower(method[0]);
    fingerprint[1] = std::tolower(method[1]);
  } else {
    // This case seems to be undefined on the spec
    fingerprint[0] = 'x';
    fingerprint[1] = 'x';
  }
  int version     = ex.get_version();
  fingerprint[2]  = 0x30 | (version >> 16);
  fingerprint[3]  = 0x30 | (version & 0xFFFF);
  fingerprint[4]  = ex.has_cookie_field() ? 'c' : 'n';
  fingerprint[5]  = ex.has_referer_field() ? 'c' : 'n';
  int field_count = ex.get_field_count();
  if (field_count < 100) {
    fingerprint[6] = 0x30 | (field_count / 10);
    fingerprint[7] = 0x30 | (field_count % 10);
  } else {
    fingerprint[6] = '9';
    fingerprint[7] = '9';
  }
  std::string_view accept_lang = ex.get_accept_language();
  if (accept_lang.empty()) {
    fingerprint[8]  = '0';
    fingerprint[9]  = '0';
    fingerprint[10] = '0';
    fingerprint[11] = '0';
  } else {
    for (int i = 0, j = 0; i < 4; ++i) {
      while (static_cast<size_t>(j) < accept_lang.size() && accept_lang[j] < 'A' && accept_lang[j] != ';') {
        ++j;
      }
      if (static_cast<size_t>(j) == accept_lang.size() || accept_lang[j] == ';') {
        fingerprint[8 + i] = '0';
      } else {
        fingerprint[8 + i] = std::tolower(accept_lang[j]);
        ++j;
      }
    }
  }

  fingerprint[12] = '_';

  // JA4H_b
  unsigned char hash[32];
  ex.get_headers_hash(hash);
  for (int i = 0; i < 6; ++i) {
    unsigned int h                = hash[i] >> 4;
    unsigned int l                = hash[i] & 0x0F;
    fingerprint[13 + (i * 2)]     = h <= 9 ? (0x30 + h) : (0x60 + h - 10);
    fingerprint[13 + (i * 2) + 1] = l <= 9 ? (0x30 + l) : (0x60 + l - 10);
  }

  fingerprint[25] = '_';

  // JA4H_c
  // Not implemented
  for (int i = 0; i < 12; ++i) {
    fingerprint[26 + i] = '0';
  }

  fingerprint[38] = '_';

  // JA4H_d
  // Not implemented
  for (int i = 0; i < 12; ++i) {
    fingerprint[39 + i] = '0';
  }

  return {fingerprint, JA4H_FINGERPRINT_LENGTH};
}

void
ja4h_method::on_request(JAxContext *ctx, TSHttpTxn txnp)
{
  ctx->set_fingerprint(get_fingerprint(txnp));
}
