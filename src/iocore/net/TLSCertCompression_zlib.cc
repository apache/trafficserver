/** @file

  Functions for zlib compression/decompression

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

#include "TLSCertCompression_zlib.h"
#include "TLSCertCompression.h"
#include "SSLStats.h"
#include <openssl/ssl.h>
#include <zlib.h>
#include <cstring>

int
compression_func_zlib(SSL *ssl, CBB *out, const uint8_t *in, size_t in_len)
{
  auto *cache = cert_compress_cache_get(SSL_get_SSL_CTX(ssl));

  if (cache) {
    auto const *entry = cache->slots[CERT_COMPRESS_ALG_ZLIB].live.load(std::memory_order_acquire);
    if (entry) {
      uint8_t *buf;
      if (CBB_reserve(out, &buf, entry->bytes.size()) != 1) {
        Metrics::Counter::increment(ssl_rsb.cert_compress_zlib_failure);
        return 0;
      }
      memcpy(buf, entry->bytes.data(), entry->bytes.size());
      CBB_did_write(out, entry->bytes.size());
      Metrics::Counter::increment(ssl_rsb.cert_compress_zlib);
      Metrics::Counter::increment(ssl_rsb.cert_compress_cache_hit);
      return 1;
    }
  }

  uint8_t      *buf;
  unsigned long buf_len = compressBound(in_len);

  if (CBB_reserve(out, &buf, buf_len) != 1) {
    Metrics::Counter::increment(ssl_rsb.cert_compress_zlib_failure);
    return 0;
  }

  if (compress(buf, &buf_len, in, in_len) == Z_OK) {
    CBB_did_write(out, buf_len);
    Metrics::Counter::increment(ssl_rsb.cert_compress_zlib);

    if (cache) {
      auto *fresh = new CertCompressionCache::Entry();
      fresh->bytes.assign(buf, buf + buf_len);
      cert_compress_cache_try_publish(cache->slots[CERT_COMPRESS_ALG_ZLIB], fresh);
    }

    return 1;
  } else {
    CBB_did_write(out, 0);
    Metrics::Counter::increment(ssl_rsb.cert_compress_zlib_failure);
    return 0;
  }
}

int
decompression_func_zlib(SSL * /* ssl */, CRYPTO_BUFFER **out, size_t uncompressed_len, const uint8_t *in, size_t in_len)
{
  if (uncompressed_len > MAX_CERT_UNCOMPRESSED_LEN) {
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zlib_failure);
    return 0;
  }

  uint8_t *buf;

  *out = CRYPTO_BUFFER_alloc(&buf, uncompressed_len);
  if (*out == nullptr) {
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zlib_failure);
    return 0;
  }

  unsigned long dest_len = uncompressed_len;

  if (uncompress(buf, &dest_len, in, in_len) != Z_OK || dest_len != uncompressed_len) {
    CRYPTO_BUFFER_free(*out);
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zlib_failure);
    return 0;
  }

  Metrics::Counter::increment(ssl_rsb.cert_decompress_zlib);
  return 1;
}
