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

int
register_certificate_compression_preference(SSL_CTX *ctx, const std::vector<std::string> &specified_algs)
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
  return SSL_CTX_set1_cert_comp_preference(ctx, algs, n);
#else
  // If Certificate Compression is unsupported there's nothing to do.
  // No need to raise an error since handshake would be done successfully without compression.
  Dbg(dbg_ctl_ssl_cert_compress, "Certificate Compression is unsupported");
  return 1;
#endif
}
