/** @file

  Unit tests for the TLS Certificate Compression cache.

  The uncompressed Certificate message for one SSL_CTX varies with the OCSP
  staple and SCT list sent to each client, so the cache must only serve an
  entry compressed from exactly the same input.

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

#include "tscore/ink_config.h"

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG

#include "../TLSCertCompression.h"
#include "../TLSCertCompression_zlib.h"
#include "../SSLStats.h"

#include <openssl/ssl.h>
#include <openssl/bytestring.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace
{
/// An SSL_CTX registered for cached zlib certificate compression, plus an SSL to drive the callbacks with.
struct CachedZlibCtx {
  SSL_CTX *ctx{nullptr};
  SSL     *ssl{nullptr};

  CachedZlibCtx()
  {
    static bool stats_initialized = false;
    if (!stats_initialized) {
      SSLInitializeStatistics();
      stats_initialized = true;
    }
    cert_compress_cache_init();
    ctx = SSL_CTX_new(TLS_method());
    REQUIRE(ctx != nullptr);
    REQUIRE(register_certificate_compression_preference(ctx, {"zlib"}, true) == 1);
    ssl = SSL_new(ctx);
    REQUIRE(ssl != nullptr);
  }
  ~CachedZlibCtx()
  {
    SSL_free(ssl);
    SSL_CTX_free(ctx);
  }
  CachedZlibCtx(CachedZlibCtx const &)            = delete;
  CachedZlibCtx &operator=(CachedZlibCtx const &) = delete;
};

/// A stand-in Certificate message; @a variant changes the trailing bytes the way a different staple would.
std::vector<uint8_t>
make_message(uint8_t variant)
{
  std::vector<uint8_t> message(2048);
  for (size_t i = 0; i < message.size(); ++i) {
    message[i] = static_cast<uint8_t>(i % 97);
  }
  for (size_t i = message.size() - 64; i < message.size(); ++i) {
    message[i] = variant;
  }
  return message;
}

/// Run the production zlib compression callback on @a input and return what a peer would decompress.
std::vector<uint8_t>
round_trip(SSL *ssl, std::vector<uint8_t> const &input)
{
  CBB cbb;
  REQUIRE(CBB_init(&cbb, 4096) == 1);
  REQUIRE(compression_func_zlib(ssl, &cbb, input.data(), input.size()) == 1);

  CRYPTO_BUFFER *out = nullptr;
  REQUIRE(decompression_func_zlib(ssl, &out, input.size(), CBB_data(&cbb), CBB_len(&cbb)) == 1);
  CBB_cleanup(&cbb);
  REQUIRE(out != nullptr);

  std::vector<uint8_t> result(CRYPTO_BUFFER_data(out), CRYPTO_BUFFER_data(out) + CRYPTO_BUFFER_len(out));
  CRYPTO_BUFFER_free(out);
  return result;
}

uint64_t
cache_hits()
{
  return Metrics::Counter::load(ssl_rsb.cert_compress_cache_hit);
}
} // end anonymous namespace

TEST_CASE("Cert compression cache serves only the matching input", "[net][cert_compress]")
{
  CachedZlibCtx b;
  auto const    with_staple    = make_message(1);
  auto const    without_staple = make_message(2);

  CHECK(round_trip(b.ssl, with_staple) == with_staple);
  CHECK(round_trip(b.ssl, without_staple) == without_staple);

  // Both variants are now cached and each is served for its own input.
  auto const hits = cache_hits();
  CHECK(round_trip(b.ssl, with_staple) == with_staple);
  CHECK(round_trip(b.ssl, without_staple) == without_staple);
  CHECK(cache_hits() == hits + 2);
}

TEST_CASE("Cert compression cache ignores an entry published after invalidation", "[net][cert_compress]")
{
  CachedZlibCtx b;
  auto const    old_staple = make_message(1);
  auto const    new_staple = make_message(2);

  CHECK(round_trip(b.ssl, old_staple) == old_staple);

  // A staple refresh invalidates the cache, then a handshake that picked up the old staple before the refresh finishes
  // compressing and publishes it.
  cert_compress_cache_invalidate(b.ctx);
  CHECK(round_trip(b.ssl, old_staple) == old_staple);

  // Handshakes with the new staple must not be served the old one.
  CHECK(round_trip(b.ssl, new_staple) == new_staple);
  auto const hits = cache_hits();
  CHECK(round_trip(b.ssl, new_staple) == new_staple);
  CHECK(cache_hits() == hits + 1);
}

TEST_CASE("Cert compression cache still compresses when every variant is taken", "[net][cert_compress]")
{
  CachedZlibCtx b;

  for (int i = 0; i < CERT_COMPRESS_VARIANTS; ++i) {
    auto const message = make_message(static_cast<uint8_t>(10 + i));
    CHECK(round_trip(b.ssl, message) == message);
  }

  auto const extra = make_message(99);
  auto const hits  = cache_hits();
  CHECK(round_trip(b.ssl, extra) == extra);
  CHECK(round_trip(b.ssl, extra) == extra);
  CHECK(cache_hits() == hits);
}

#endif // HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
