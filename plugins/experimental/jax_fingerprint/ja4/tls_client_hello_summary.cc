/** @file
 *
  TLSClientHelloSummary data structure for JA4 fingerprint calculation.

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
#include "ja4.h"

#include "tls_client_hello_summary.h"

#include <openssl/sha.h>
#include <cstdint>

TLSClientHelloSummary::TLSClientHelloSummary(ja4::Datasource::Protocol protocol, TSClientHello ch) : _ch(ch)
{
  const uint8_t *buf;
  size_t         buflen;

  // Protocol
  this->_protocol = protocol;

  // Version
  if (TS_SUCCESS == TSClientHelloExtensionGet(this->_ch, EXT_SUPPORTED_VERSIONS, &buf, &buflen)) {
    uint16_t max_version{0};
    size_t   versions_len = buf[0];

    if (buflen < versions_len + 1) {
      Dbg(dbg_ctl, "Malformed supported_versions extension (truncated vector)... using legacy version.");
      this->_version = this->_ch.get_version();
    } else {
      for (size_t i = 1; (i + 1) < (versions_len + 1); i += 2) {
        uint16_t version = (buf[i] << 8) | buf[i + 1];
        if (!this->_is_GREASE(version) && version > max_version) {
          max_version = version;
        }
      }
      this->_version = max_version;
    }
  } else {
    Dbg(dbg_ctl, "No supported_versions extension... using legacy version.");
    this->_version = this->_ch.get_version();
  }

  // Ciphers
  buf    = this->_ch.get_cipher_suites();
  buflen = this->_ch.get_cipher_suites_len();

  if (buflen / 2 <= MAX_CIPHERS_FOR_FAST_PATH) {
    // Fast path
    this->_ciphers = this->_fast_cipher_storage.data();
  } else {
    // Slow path
    this->_slow_cipher_storage = std::make_unique<uint16_t[]>(buflen / 2);
    this->_ciphers             = this->_slow_cipher_storage.get();
  }
  for (size_t i = 0; i + 1 < buflen; i += 2) {
    uint16_t cipher = (static_cast<uint16_t>(buf[i]) << 8) + buf[i + 1];
    if (this->_is_GREASE(cipher)) {
      continue;
    }
    this->_ciphers[this->_n_ciphers++] = cipher;
  }
  std::sort(this->_ciphers, this->_ciphers + this->_n_ciphers);

  // Extensions
  auto count = 0;
  for (auto &&type : this->_ch.get_extension_types()) {
    (void)type;
    ++count;
  }
  if (count <= MAX_EXTENSIONS_FOR_FAST_PATH) {
    // Fast path
    this->_extensions = this->_fast_extension_storage.data();
  } else {
    // Slow path
    this->_slow_extension_storage = std::make_unique<uint16_t[]>(count);
    this->_extensions             = this->_slow_extension_storage.get();
  }
  for (auto &&type : this->_ch.get_extension_types()) {
    if (type == EXT_SNI) {
      this->_has_SNI = true;
      continue;
    }
    if (type == EXT_ALPN) {
      this->_has_ALPN = true;
      continue;
    }
    if (this->_is_GREASE(type)) {
      continue;
    }
    this->_extensions[this->_n_extensions++] = type;
  }
  std::sort(this->_extensions, this->_extensions + this->_n_extensions);
}

std::string_view
TLSClientHelloSummary::get_first_alpn()
{
  unsigned char const *buf{};
  std::size_t          buflen{};

  if (TS_SUCCESS == TSClientHelloExtensionGet(this->_ch, EXT_ALPN, &buf, &buflen)) {
    // The first two bytes are a 16bit encoding of the total length.
    if (buflen < 4) {
      return {};
    }

    unsigned char first_ALPN_length = buf[2];
    if (first_ALPN_length == 0 || first_ALPN_length > (buflen - 3)) {
      return {};
    }

    return {reinterpret_cast<const char *>(&(buf[3])), first_ALPN_length};
  } else {
    return {};
  }
}

void
TLSClientHelloSummary::get_cipher_suites_hash(unsigned char out[32])
{
  if (this->_n_ciphers == 0) {
    memset(out, 0, 32);
    return;
  }

  SHA256_CTX sha256ctx;
  SHA256_Init(&sha256ctx);

  for (int i = 0; i < this->_n_ciphers; ++i) {
    char  buf[5];
    char *p = buf;
    if (i != 0) {
      *p  = ',';
      p  += 1;
    }
    uint16_t &cipher  = this->_ciphers[i];
    uint8_t   h1      = (cipher & 0xF000) >> 12;
    uint8_t   l1      = (cipher & 0x0F00) >> 8;
    uint8_t   h2      = (cipher & 0x00F0) >> 4;
    uint8_t   l2      = cipher & 0x000F;
    p[0]              = h1 <= 9 ? ('0' + h1) : ('a' + h1 - 10);
    p[1]              = l1 <= 9 ? ('0' + l1) : ('a' + l1 - 10);
    p[2]              = h2 <= 9 ? ('0' + h2) : ('a' + h2 - 10);
    p[3]              = l2 <= 9 ? ('0' + l2) : ('a' + l2 - 10);
    p                += 4;
    SHA256_Update(&sha256ctx, buf, p - buf);
  }

  SHA256_Final(out, &sha256ctx);
}

void
TLSClientHelloSummary::get_extension_hash(unsigned char out[32])
{
  if (this->_n_extensions == 0) {
    memset(out, 0, 32);
    return;
  }

  SHA256_CTX sha256ctx;
  SHA256_Init(&sha256ctx);

  for (int i = 0; i < this->_n_extensions; ++i) {
    char  buf[5];
    char *p = buf;
    if (i != 0) {
      *p  = ',';
      p  += 1;
    }
    uint16_t &extension  = this->_extensions[i];
    uint8_t   h1         = (extension & 0xF000) >> 12;
    uint8_t   l1         = (extension & 0x0F00) >> 8;
    uint8_t   h2         = (extension & 0x00F0) >> 4;
    uint8_t   l2         = extension & 0x000F;
    p[0]                 = h1 <= 9 ? ('0' + h1) : ('a' + h1 - 10);
    p[1]                 = l1 <= 9 ? ('0' + l1) : ('a' + l1 - 10);
    p[2]                 = h2 <= 9 ? ('0' + h2) : ('a' + h2 - 10);
    p[3]                 = l2 <= 9 ? ('0' + l2) : ('a' + l2 - 10);
    p                   += 4;
    SHA256_Update(&sha256ctx, buf, p - buf);
  }

  SHA256_Final(out, &sha256ctx);
}
