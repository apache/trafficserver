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

#include <plugin.h>
#include <context.h>
#include "ja4_method.h"
#include "ja4.h"

#include <openssl/sha.h>
#include <openssl/ssl.h>

constexpr unsigned int EXT_ALPN{0x10};
constexpr unsigned int EXT_SUPPORTED_VERSIONS{0x2b};

namespace ja4_method
{

void on_client_hello(JAxContext *, TSVConn);

struct Method method = {
  "JA4",
  Method::Type::CONNECTION_BASED,
  on_client_hello,
  nullptr,
};

} // namespace ja4_method

static std::uint16_t
get_version(TSClientHello ch)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  if (TS_SUCCESS == TSClientHelloExtensionGet(ch, EXT_SUPPORTED_VERSIONS, &buf, &buflen)) {
    std::uint16_t max_version{0};
    size_t        versions_len = buf[0];

    if (buflen < versions_len + 1) {
      Dbg(dbg_ctl, "Malformed supported_versions extension (truncated vector)... using legacy version.");
      return ch.get_version();
    }

    for (size_t i = 1; (i + 1) < (versions_len + 1); i += 2) {
      std::uint16_t version = (buf[i] << 8) | buf[i + 1];
      if (!JA4::is_GREASE(version) && version > max_version) {
        max_version = version;
      }
    }
    return max_version;
  } else {
    Dbg(dbg_ctl, "No supported_versions extension... using legacy version.");
    return ch.get_version();
  }
}

static std::string
get_first_ALPN(TSClientHello ch)
{
  unsigned char const *buf{};
  std::size_t          buflen{};
  std::string          result{""};
  if (TS_SUCCESS == TSClientHelloExtensionGet(ch, EXT_ALPN, &buf, &buflen)) {
    // The first two bytes are a 16bit encoding of the total length.
    unsigned char first_ALPN_length{buf[2]};
    TSAssert(buflen > 4);
    TSAssert(0 != first_ALPN_length);
    result.assign(&buf[3], (&buf[3]) + first_ALPN_length);
  }

  return result;
}

static constexpr std::uint16_t
make_word(unsigned char lowbyte, unsigned char highbyte)
{
  return (static_cast<std::uint16_t>(highbyte) << 8) | lowbyte;
}

static void
add_ciphers(JA4::TLSClientHelloSummary &summary, TSClientHello ch)
{
  const uint8_t *buf    = ch.get_cipher_suites();
  size_t         buflen = ch.get_cipher_suites_len();

  if (buflen > 0) {
    for (std::size_t i = 0; i + 1 < buflen; i += 2) {
      summary.add_cipher(make_word(buf[i], buf[i + 1]));
    }
  } else {
    Dbg(dbg_ctl, "Failed to get ciphers.");
  }
}

static void
add_extensions(JA4::TLSClientHelloSummary &summary, TSClientHello ch)
{
  for (auto ext_type : ch.get_extension_types()) {
    summary.add_extension(ext_type);
  }
}

static std::string
hash_with_SHA256(std::string_view sv)
{
  Dbg(dbg_ctl, "Hashing %s", std::string{sv}.c_str());
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<unsigned char const *>(sv.data()), sv.size(), hash);
  std::string result;
  result.resize(SHA256_DIGEST_LENGTH * 2 + 1);
  for (int i{0}; i < SHA256_DIGEST_LENGTH; ++i) {
    std::snprintf(result.data() + (i * 2), result.size() - (i * 2), "%02x", hash[i]);
  }
  return result;
}

static std::string
get_fingerprint(TSClientHello ch)
{
  JA4::TLSClientHelloSummary summary{};
  summary.protocol    = JA4::Protocol::TLS;
  summary.TLS_version = get_version(ch);
  summary.ALPN        = get_first_ALPN(ch);
  add_ciphers(summary, ch);
  add_extensions(summary, ch);
  std::string result{JA4::make_JA4_fingerprint(summary, hash_with_SHA256)};
  return result;
}

void
ja4_method::on_client_hello(JAxContext *ctx, TSVConn vconn)
{
  TSClientHello ch = TSVConnClientHelloGet(vconn);

  if (!ch) {
    Dbg(dbg_ctl, "Could not get TSClientHello object.");
  } else {
    ctx->set_fingerprint(get_fingerprint(ch));
  }
}
