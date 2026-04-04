/** @file

  Functions for brotli compression/decompression

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

#include "TLSCertCompression_brotli.h"
#include "TLSCertCompression.h"
#include "SSLStats.h"
#include <openssl/ssl.h>
#include <brotli/decode.h>
#include <brotli/encode.h>

int
compression_func_brotli(SSL *ssl, CBB *out, const uint8_t *in, size_t in_len)
{
  auto *cache = cert_compress_cache_get(SSL_get_SSL_CTX(ssl));

  if (cache) {
    auto const *entry = cache->slots[CERT_COMPRESS_ALG_BROTLI].live.load(std::memory_order_acquire);
    if (entry) {
      uint8_t *buf;
      if (CBB_reserve(out, &buf, entry->bytes.size()) != 1) {
        Metrics::Counter::increment(ssl_rsb.cert_compress_brotli_failure);
        return 0;
      }
      memcpy(buf, entry->bytes.data(), entry->bytes.size());
      CBB_did_write(out, entry->bytes.size());
      Metrics::Counter::increment(ssl_rsb.cert_compress_brotli);
      Metrics::Counter::increment(ssl_rsb.cert_compress_cache_hit);
      return 1;
    }
  }

  uint8_t      *buf;
  unsigned long buf_len = BrotliEncoderMaxCompressedSize(in_len);

  if (CBB_reserve(out, &buf, buf_len) != 1) {
    Metrics::Counter::increment(ssl_rsb.cert_compress_brotli_failure);
    return 0;
  }

  if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE, in_len, in, &buf_len, buf) ==
      BROTLI_TRUE) {
    CBB_did_write(out, buf_len);
    Metrics::Counter::increment(ssl_rsb.cert_compress_brotli);

    if (cache) {
      auto *fresh = new CertCompressionCache::Entry();
      fresh->bytes.assign(buf, buf + buf_len);
      cert_compress_cache_try_publish(cache->slots[CERT_COMPRESS_ALG_BROTLI], fresh);
    }

    return 1;
  } else {
    CBB_did_write(out, 0);
    Metrics::Counter::increment(ssl_rsb.cert_compress_brotli_failure);
    return 0;
  }
}

int
decompression_func_brotli(SSL * /* ssl */, CRYPTO_BUFFER **out, size_t uncompressed_len, const uint8_t *in, size_t in_len)
{
  if (uncompressed_len > MAX_CERT_UNCOMPRESSED_LEN) {
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_brotli_failure);
    return 0;
  }

  uint8_t *buf;

  *out = CRYPTO_BUFFER_alloc(&buf, uncompressed_len);
  if (*out == nullptr) {
    Metrics::Counter::increment(ssl_rsb.cert_decompress_brotli_failure);
    return 0;
  }

  size_t dest_len = uncompressed_len;

  if (BrotliDecoderDecompress(in_len, in, &dest_len, buf) != BROTLI_DECODER_RESULT_SUCCESS || dest_len != uncompressed_len) {
    CRYPTO_BUFFER_free(*out);
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_brotli_failure);
    return 0;
  }

  Metrics::Counter::increment(ssl_rsb.cert_decompress_brotli);
  return 1;
}
