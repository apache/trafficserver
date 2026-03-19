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
#include "ja3_method.h"
#include "ja3_utils.h"

#include <openssl/ssl.h>
#include <openssl/md5.h>
#include <openssl/opensslv.h>

#include <algorithm>

namespace ja3_method
{

void on_client_hello(JAxContext *, TSVConn);

struct Method method = {
  "JA3",
  Method::Type::CONNECTION_BASED,
  on_client_hello,
  nullptr,
};

} // namespace ja3_method

namespace
{
constexpr int ja3_hash_included_byte_count{16};
static_assert(ja3_hash_included_byte_count <= MD5_DIGEST_LENGTH);

constexpr int ja3_hash_hex_string_with_null_terminator_length{2 * ja3_hash_included_byte_count + 1};

} // end anonymous namespace

static std::string
get_fingerprint(TSClientHello ch)
{
  std::string          raw;
  std::size_t          len{};
  const unsigned char *buf{};

  // Get version
  unsigned int version = ch.get_version();
  raw.append(std::to_string(version));
  raw.push_back(',');

  // Get cipher suites
  raw.append(ja3::encode_word_buffer(ch.get_cipher_suites(), ch.get_cipher_suites_len()));
  raw.push_back(',');

  // Get extensions
  auto ext_types = ch.get_extension_types();
  len            = 0;
  auto first     = ext_types.begin();
  auto last      = ext_types.end();
  while (first != last) {
    ++first;
    ++len;
  }
  if (len > 0) {
    int extension_ids[len];
    first = ext_types.begin();
    for (size_t i = 0; i < len; ++i, ++first) {
      extension_ids[i] = *first;
    }
    raw.append(ja3::encode_integer_buffer(extension_ids, len));
  }
  raw.push_back(',');

  // Get elliptic curves
  if (TS_SUCCESS == TSClientHelloExtensionGet(ch, 0x0a, &buf, &len) && len >= 2) {
    // Skip first 2 bytes since we already have length
    raw.append(ja3::encode_word_buffer(buf + 2, len - 2));
  }
  raw.push_back(',');

  // Get elliptic curve point formats
  if (TS_SUCCESS == TSClientHelloExtensionGet(ch, 0x0b, &buf, &len) && len >= 2) {
    // Skip first byte since we already have length
    raw.append(ja3::encode_byte_buffer(buf + 1, len - 1));
  }
  Dbg(dbg_ctl, "Hashing %s", raw.c_str());

  char          fingerprint[ja3_hash_hex_string_with_null_terminator_length];
  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<unsigned char const *>(raw.c_str()), raw.length(), digest);
  for (int i{0}; i < ja3_hash_included_byte_count; ++i) {
    std::snprintf(&(fingerprint[i * 2]), sizeof(fingerprint) - (i * 2), "%02x", static_cast<unsigned int>(digest[i]));
  }

  return {fingerprint};
}

void
ja3_method::on_client_hello(JAxContext *ctx, TSVConn vconn)
{
  TSClientHello ch = TSVConnClientHelloGet(vconn);

  if (!ch) {
    Dbg(dbg_ctl, "Could not get TSClientHello object.");
  } else {
    ctx->set_fingerprint(get_fingerprint(ch));
  }
}
