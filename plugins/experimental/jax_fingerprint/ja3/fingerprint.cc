/** @file

  JA3 TLS ClientHello fingerprint calculation.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements. See the NOTICE file distributed with this work for additional information regarding
  copyright ownership. Licensed under the Apache License, Version 2.0 (the "License"); you may not
  use this file except in compliance with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "fingerprint.h"
#include "utils.h"

#include <openssl/md5.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
DbgCtl dbg_ctl{"jax_fingerprint"};

constexpr int JA3_HASH_BYTE_COUNT = 16;
static_assert(JA3_HASH_BYTE_COUNT <= MD5_DIGEST_LENGTH);

constexpr int JA3_HASH_STRING_SIZE = 2 * JA3_HASH_BYTE_COUNT + 1;
} // namespace

std::string
ja3::fingerprint(TSClientHello client_hello)
{
  std::string          raw;
  std::size_t          len{};
  const unsigned char *buf{};

  raw.append(std::to_string(client_hello.get_version()));
  raw.push_back(',');
  raw.append(encode_word_buffer(client_hello.get_cipher_suites(), client_hello.get_cipher_suites_len()));
  raw.push_back(',');

  std::vector<int> extension_ids;
  for (auto extension_type : client_hello.get_extension_types()) {
    extension_ids.push_back(extension_type);
  }
  if (!extension_ids.empty()) {
    raw.append(encode_integer_buffer(extension_ids.data(), static_cast<int>(extension_ids.size())));
  }
  raw.push_back(',');

  if (TS_SUCCESS == TSClientHelloExtensionGet(client_hello, 0x0a, &buf, &len) && len >= 2) {
    raw.append(encode_word_buffer(buf + 2, len - 2));
  }
  raw.push_back(',');

  if (TS_SUCCESS == TSClientHelloExtensionGet(client_hello, 0x0b, &buf, &len) && len >= 2) {
    raw.append(encode_byte_buffer(buf + 1, len - 1));
  }
  Dbg(dbg_ctl, "Hashing %s", raw.c_str());

  char          result[JA3_HASH_STRING_SIZE]{};
  unsigned char digest[MD5_DIGEST_LENGTH];

  MD5(reinterpret_cast<unsigned char const *>(raw.data()), raw.size(), digest);
  for (int i = 0; i < JA3_HASH_BYTE_COUNT; ++i) {
    std::snprintf(result + i * 2, sizeof(result) - i * 2, "%02x", static_cast<unsigned int>(digest[i]));
  }

  return result;
}
