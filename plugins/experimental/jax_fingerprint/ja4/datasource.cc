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

#include "datasource.h"

#include <openssl/sha.h>

#include <array>
#include <algorithm>
#include <cstring>

constexpr std::array<std::uint16_t, 16> GREASE_values{0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
                                                      0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa};

ja4::Datasource::Protocol
ja4::Datasource::get_protocol()
{
  return this->_protocol;
}

int
ja4::Datasource::get_version()
{
  return this->_version;
}

ja4::Datasource::SNI
ja4::Datasource::get_sni_type()
{
  return this->_has_SNI ? ja4::Datasource::SNI::to_domain : ja4::Datasource::SNI::to_IP;
}

int
ja4::Datasource::get_cipher_count()
{
  return this->_n_ciphers;
}

int
ja4::Datasource::get_extension_count()
{
  return this->_n_extensions + (this->_has_ALPN ? 1 : 0) + (this->_has_SNI ? 1 : 0);
}

/**
 * Check whether @a value is a GREASE value.
 *
 * These are reserved extensions randomly advertised to keep implementations
 * well lubricated. They are ignored in all parts of JA4 because of their
 * random nature.
 *
 * @return Returns true if the value is a GREASE value, false otherwise.
 */
bool
ja4::Datasource::_is_GREASE(uint16_t value)
{
  return std::binary_search(GREASE_values.begin(), GREASE_values.end(), value);
}

static void
update_with_hex(SHA256_CTX &ctx, char separator, uint16_t value)
{
  char  buf[5];
  char *p = buf;

  if (separator != '\0') {
    *p++ = separator;
  }
  for (int shift = 12; shift >= 0; shift -= 4) {
    uint8_t nibble = (value >> shift) & 0xF;
    *p++           = nibble <= 9 ? ('0' + nibble) : ('a' + nibble - 10);
  }
  SHA256_Update(&ctx, buf, p - buf);
}

void
ja4::Datasource::_hash_extensions(unsigned char out[32], uint16_t const *sorted_extensions, int n_extensions,
                                  unsigned char const *sig_algs, size_t sig_algs_len)
{
  SHA256_CTX ctx;
  bool       hashed_any = false;

  SHA256_Init(&ctx);
  for (int i = 0; i < n_extensions; ++i) {
    update_with_hex(ctx, i == 0 ? '\0' : ',', sorted_extensions[i]);
    hashed_any = true;
  }

  // The extension body is a 2-byte length followed by 2-byte algorithm codes, hashed in wire order.
  if (sig_algs != nullptr && sig_algs_len >= 2) {
    size_t end         = std::min(sig_algs_len, 2 + ((static_cast<size_t>(sig_algs[0]) << 8) | sig_algs[1]));
    bool   hashed_algs = false;

    for (size_t i = 2; i + 1 < end; i += 2) {
      uint16_t alg = (static_cast<uint16_t>(sig_algs[i]) << 8) | sig_algs[i + 1];
      if (this->_is_GREASE(alg)) {
        continue;
      }
      update_with_hex(ctx, hashed_algs ? ',' : '_', alg);
      hashed_algs = true;
      hashed_any  = true;
    }
  }

  if (!hashed_any) {
    memset(out, 0, 32);
    return;
  }
  SHA256_Final(out, &ctx);
}
