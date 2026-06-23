/** @file

  Functions for Certificate Compression

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

#pragma once

#include "tscore/ink_config.h"

#include <openssl/ssl.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// RFC 8879 uses uint24 for uncompressed_length, allowing up to ~16 MB.
// Real certificate chains rarely exceed 10-30 KB even with large RSA
// keys and multiple intermediates — 128 KB gives ample headroom while
// preventing excessive memory allocation from a malicious peer.
constexpr size_t MAX_CERT_UNCOMPRESSED_LEN = 128 * 1024;

/**
 * Common function to set certificate compression preference
 *
 * @param[in] ctx SSL_CTX
 * @param[in] algs A vector that contains compression algorithm names ("zlib", "brotli", or "zstd")
 * @param[in] cache If true, cache the compressed cert chain per SSL_CTX (BoringSSL only; no-op on OpenSSL)
 * @return 1 on success
 */
int register_certificate_compression_preference(SSL_CTX *ctx, const std::vector<std::string> &algs, bool cache);

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG

// Max algorithm ID + 1, used to size the cache entry array
constexpr int CERT_COMPRESS_MAX_ALG_ID = 4;

// Keyed by (SSL_CTX, alg). Assumes one cert chain per SSL_CTX, which the
// BoringSSL build path enforces (HAVE_NATIVE_DUAL_CERT_SUPPORT is off).
struct CertCompressionCache {
  struct Entry {
    std::vector<uint8_t> bytes;
  };
  struct Slot {
    std::atomic<Entry const *> live{nullptr};
    std::atomic<Entry const *> retired{nullptr};
  };
  Slot slots[CERT_COMPRESS_MAX_ALG_ID];
};

void                  cert_compress_cache_init();
CertCompressionCache *cert_compress_cache_get(SSL_CTX *ctx);
void                  cert_compress_cache_invalidate(SSL_CTX *ctx);
void                  cert_compress_cache_try_publish(CertCompressionCache::Slot &slot, CertCompressionCache::Entry const *fresh);
#endif

#if HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
void cert_compress_compress_certs(SSL_CTX *ctx);
#endif

void cert_compress_invalidate_or_recompress(SSL_CTX *ctx);
