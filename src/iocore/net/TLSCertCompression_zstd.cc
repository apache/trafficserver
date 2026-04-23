/** @file

  Functions for zstd compression/decompression

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

#include "TLSCertCompression_zstd.h"
#include "TLSCertCompression.h"
#include "SSLStats.h"
#include <openssl/ssl.h>
#include <zstd.h>

int
compression_func_zstd(SSL * /* ssl */, CBB *out, const uint8_t *in, size_t in_len)
{
  // TODO Need a cache mechanism inside this function for better performance.

  uint8_t      *buf;
  unsigned long buf_len = ZSTD_compressBound(in_len);

  if (ZSTD_isError(buf_len) == 1) {
    Metrics::Counter::increment(ssl_rsb.cert_compress_zstd_failure);
    return 0;
  }

  if (CBB_reserve(out, &buf, buf_len) != 1) {
    Metrics::Counter::increment(ssl_rsb.cert_compress_zstd_failure);
    return 0;
  }

  // For better performance ZSTD_compressCCtx, which reuses a context object, should be used.
  // One context object need to be made for each thread.
  size_t ret = ZSTD_compress(buf, buf_len, in, in_len, ZSTD_CLEVEL_DEFAULT);
  if (ZSTD_isError(ret) == 1) {
    Metrics::Counter::increment(ssl_rsb.cert_compress_zstd_failure);
    return 0;
  } else {
    CBB_did_write(out, ret);
    Metrics::Counter::increment(ssl_rsb.cert_compress_zstd);
    return 1;
  }
}

int
decompression_func_zstd(SSL * /* ssl */, CRYPTO_BUFFER **out, size_t uncompressed_len, const uint8_t *in, size_t in_len)
{
  if (uncompressed_len > MAX_CERT_UNCOMPRESSED_LEN) {
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zstd_failure);
    return 0;
  }

  uint8_t *buf;

  *out = CRYPTO_BUFFER_alloc(&buf, uncompressed_len);
  if (*out == nullptr) {
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zstd_failure);
    return 0;
  }

  size_t ret = ZSTD_decompress(buf, uncompressed_len, in, in_len);

  if (ZSTD_isError(ret) || ret != uncompressed_len) {
    CRYPTO_BUFFER_free(*out);
    *out = nullptr;
    Metrics::Counter::increment(ssl_rsb.cert_decompress_zstd_failure);
    return 0;
  }

  Metrics::Counter::increment(ssl_rsb.cert_decompress_zstd);
  return 1;
}
