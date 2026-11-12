/** @file

  Functions for Certificate Compression

  @par License

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

// Distinct uncompressed Certificate messages cached per (SSL_CTX, alg). One SSL_CTX carries one cert chain (the BoringSSL
// build path keeps HAVE_NATIVE_DUAL_CERT_SUPPORT off), but the message also carries the OCSP staple and SCT list, which
// are only included when the client asks for them and change when the staple is refreshed.
constexpr int CERT_COMPRESS_VARIANTS = 4;

// Compressed Certificate messages per SSL_CTX, keyed by algorithm and by the exact uncompressed message. Entries are only
// freed by cert_compress_cache_invalidate() (one invalidation after they are unpublished) and when the SSL_CTX is freed.
struct CertCompressionCache {
  struct Entry {
    std::vector<uint8_t> input; ///< Uncompressed Certificate message this entry was compressed from.
    std::vector<uint8_t> bytes; ///< Compressed form of @a input.
  };
  struct Slot {
    std::atomic<Entry const *> live{nullptr};
    std::atomic<Entry const *> retired{nullptr};
  };
  Slot slots[CERT_COMPRESS_MAX_ALG_ID][CERT_COMPRESS_VARIANTS];
};

void                  cert_compress_cache_init();
CertCompressionCache *cert_compress_cache_get(SSL_CTX *ctx);
void                  cert_compress_cache_invalidate(SSL_CTX *ctx);

/** Find the cached compression of exactly @a in for algorithm @a alg.
 *
 * @return The matching entry, or @c nullptr on a miss. The entry is not freed until the second
 *   cert_compress_cache_invalidate() after it is returned.
 */
CertCompressionCache::Entry const *cert_compress_cache_find(CertCompressionCache &cache, int alg, const uint8_t *in, size_t in_len);

/** Cache @a compressed as the compression of @a in for algorithm @a alg if a variant slot is free.
 *
 * Nothing is cached if every variant slot is taken; the caller already wrote @a compressed to the handshake either way.
 */
void cert_compress_cache_try_publish(CertCompressionCache &cache, int alg, const uint8_t *in, size_t in_len,
                                     const uint8_t *compressed, size_t compressed_len);
#endif

#if HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
void cert_compress_compress_certs(SSL_CTX *ctx);
#endif

void cert_compress_invalidate_or_recompress(SSL_CTX *ctx);
