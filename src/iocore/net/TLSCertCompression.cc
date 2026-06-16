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

#include <openssl/ssl.h>

#include "tscore/Diags.h"
#include "TLSCertCompression.h"

namespace
{
DbgCtl dbg_ctl_ssl_cert_compress{"ssl_cert_compress"};
}

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG || HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
constexpr unsigned int N_ALGORITHMS = 3;
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
#include "TLSCertCompression_zlib.h"

#if HAVE_BROTLI_ENCODE_H
#include "TLSCertCompression_brotli.h"
#endif

#if HAVE_ZSTD_H
#include "TLSCertCompression_zstd.h"
#endif
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG || HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
namespace
{
struct alg_info {
  const char *name;
  int32_t     number;
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  ssl_cert_compression_func_t   compress_func;
  ssl_cert_decompression_func_t decompress_func;
#endif
} supported_algs[] = {
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  {"zlib",   1, compression_func_zlib,   decompression_func_zlib  },
#if HAVE_BROTLI_ENCODE_H
  {"brotli", 2, compression_func_brotli, decompression_func_brotli},
#endif
#if HAVE_ZSTD_H
  {"zstd",   3, compression_func_zstd,   decompression_func_zstd  },
#endif
  {nullptr,  0, nullptr,                 nullptr                  },
#elif HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
#if !defined(OPENSSL_NO_ZLIB)
  {"zlib", 1},
#endif
#if !defined(OPENSSL_NO_BROTLI)
  {"brotli", 2},
#endif
#if !defined(OPENSSL_NO_ZSTD)
  {"zstd", 3},
#endif
  {nullptr, 0},
#else
  {nullptr, 0},
#endif
};

alg_info const *
find_algorithm(std::string const &name)
{
  for (auto const &alg : supported_algs) {
    if (alg.name != nullptr && name == alg.name) {
      return &alg;
    }
  }
  return nullptr;
}

} // end anonymous namespace
#endif

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
static int cert_compress_cache_index = -1;

static void
cert_compress_cache_free_cb(void * /* parent */, void *ptr, CRYPTO_EX_DATA * /* ad */, int /* idx */, long /* argl */,
                            void * /* argp */)
{
  auto *cache = static_cast<CertCompressionCache *>(ptr);
  if (cache) {
    for (auto &slot : cache->slots) {
      delete slot.live.load(std::memory_order_acquire);
      delete slot.retired.load(std::memory_order_acquire);
    }
    delete cache;
  }
}

void
cert_compress_cache_init()
{
  if (cert_compress_cache_index != -1) {
    return;
  }
  cert_compress_cache_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, cert_compress_cache_free_cb);
}

CertCompressionCache *
cert_compress_cache_get(SSL_CTX *ctx)
{
  if (cert_compress_cache_index < 0) {
    return nullptr;
  }
  return static_cast<CertCompressionCache *>(SSL_CTX_get_ex_data(ctx, cert_compress_cache_index));
}

static void
cert_compress_cache_attach(SSL_CTX *ctx)
{
  if (cert_compress_cache_index < 0) {
    return;
  }
  auto *cache = new CertCompressionCache();
  if (SSL_CTX_set_ex_data(ctx, cert_compress_cache_index, cache) != 1) {
    delete cache;
  }
}

void
cert_compress_cache_try_publish(CertCompressionCache::Slot &slot, CertCompressionCache::Entry const *fresh)
{
  CertCompressionCache::Entry const *expected = nullptr;
  if (!slot.live.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
    delete fresh;
  }
}

void
cert_compress_cache_invalidate(SSL_CTX *ctx)
{
  auto *cache = cert_compress_cache_get(ctx);
  if (!cache) {
    return;
  }
  for (auto &slot : cache->slots) {
    auto const *prev    = slot.live.exchange(nullptr, std::memory_order_acq_rel);
    auto const *to_free = slot.retired.exchange(prev, std::memory_order_acq_rel);
    delete to_free;
  }
  Dbg(dbg_ctl_ssl_cert_compress, "Cache invalidated for SSL_CTX %p", ctx);
}
#endif

#if HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
void
cert_compress_compress_certs(SSL_CTX *ctx)
{
  for (unsigned int i = 0; i < countof(supported_algs); ++i) {
    if (supported_algs[i].name == nullptr) {
      continue;
    }
    if (SSL_CTX_compress_certs(ctx, supported_algs[i].number)) {
      Dbg(dbg_ctl_ssl_cert_compress, "Pre-compressed certs for alg %s on SSL_CTX %p", supported_algs[i].name, ctx);
    }
  }
}
#endif

void
cert_compress_invalidate_or_recompress(SSL_CTX *ctx)
{
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  cert_compress_cache_invalidate(ctx);
#elif HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
  cert_compress_compress_certs(ctx);
#else
  (void)ctx;
#endif
}

int
register_certificate_compression_preference(SSL_CTX *ctx, const std::vector<std::string> &specified_algs, bool cache)
{
  ink_assert(ctx != nullptr);
  if (specified_algs.empty()) {
    return 1;
  }

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  if (specified_algs.size() > N_ALGORITHMS) {
    return 0;
  }

  for (auto &&alg : specified_algs) {
    auto const *info = find_algorithm(alg);
    if (info == nullptr) {
      Dbg(dbg_ctl_ssl_cert_compress, "Unsupported algorithm: %s", alg.c_str());
      return 0;
    }
    if (SSL_CTX_add_cert_compression_alg(ctx, info->number, info->compress_func, info->decompress_func) == 0) {
      return 0;
    }
    Dbg(dbg_ctl_ssl_cert_compress, "Enabled %s", info->name);
  }

  if (cache) {
    cert_compress_cache_attach(ctx);
  }

  return 1;
#elif HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
  if (specified_algs.size() > N_ALGORITHMS) {
    return 0;
  }

  int algs[N_ALGORITHMS];
  int n = 0;

  for (unsigned int i = 0; i < specified_algs.size(); ++i) {
    auto const *info = find_algorithm(specified_algs[i]);
    if (info == nullptr) {
      Dbg(dbg_ctl_ssl_cert_compress, "Unsupported algorithm: %s", specified_algs[i].c_str());
      return 0;
    }
    algs[n++] = info->number;
    Dbg(dbg_ctl_ssl_cert_compress, "Enabled %s", info->name);
  }
  int ret = SSL_CTX_set1_cert_comp_preference(ctx, algs, n);
  if (ret == 1) {
    cert_compress_compress_certs(ctx);
  }
  return ret;
#else
  // If Certificate Compression is unsupported there's nothing to do.
  // No need to raise an error since handshake would be done successfully without compression.
  Dbg(dbg_ctl_ssl_cert_compress, "Certificate Compression is unsupported");
  return 1;
#endif
}
