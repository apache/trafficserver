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

constexpr unsigned int N_ALGORITHMS = 3;

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
#include "TLSCertCompression_zlib.h"

#if HAVE_BROTLI_ENCODE_H
#include "TLSCertCompression_brotli.h"
#endif

#if HAVE_ZSTD_H
#include "TLSCertCompression_zstd.h"
#endif
#endif

struct alg_info {
  const char *name;
  int32_t     number;
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  ssl_cert_compression_func_t   compress_func;
  ssl_cert_decompression_func_t decompress_func;
#endif
} supported_algs[] = {
  {"zlib",   1,
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
   compression_func_zlib,   decompression_func_zlib
#endif
  },
#if HAVE_BROTLI_ENCODE_H
  {"brotli", 2,
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
   compression_func_brotli, decompression_func_brotli
#endif
  },
#endif
#if HAVE_ZSTD_H
  {"zstd",   3,
#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
   compression_func_zstd,   decompression_func_zstd
#endif
  },
#endif
};

int
register_certificate_compression_preference(SSL_CTX *ctx, const std::vector<std::string> &specified_algs)
{
  ink_assert(ctx != nullptr);
  if (specified_algs.size() > N_ALGORITHMS) {
    return 0;
  }

  if (specified_algs.empty()) {
    return 1;
  }

#if HAVE_SSL_CTX_ADD_CERT_COMPRESSION_ALG
  for (auto &&alg : specified_algs) {
    struct alg_info *info = nullptr;

    for (unsigned int i = 0; i < countof(supported_algs); ++i) {
      if (strcmp(alg.c_str(), supported_algs[i].name) == 0) {
        info = &supported_algs[i];
      }
    }
    if (info != nullptr) {
      if (SSL_CTX_add_cert_compression_alg(ctx, info->number, info->compress_func, info->decompress_func) == 0) {
        return 0;
      }
      Dbg(dbg_ctl_ssl_cert_compress, "Enabled %s", info->name);
    } else {
      Dbg(dbg_ctl_ssl_cert_compress, "Unrecognized algorithm: %s", alg.c_str());
      return 0;
    }
  }
  return 1;
#elif HAVE_SSL_CTX_SET1_CERT_COMP_PREFERENCE
  int algs[N_ALGORITHMS];
  int n = 0;

  for (unsigned int i = 0; i < specified_algs.size(); ++i) {
    for (unsigned int j = 0; j < countof(supported_algs); ++j) {
      if (strcmp(specified_algs[i].c_str(), supported_algs[j].name) == 0) {
        algs[n++] = supported_algs[j].number;
        Dbg(dbg_ctl_ssl_cert_compress, "Enabled %s", supported_algs[j].name);
      }
    }
  }
  return SSL_CTX_set1_cert_comp_preference(ctx, algs, n);
#else
  // If Certificate Compression is unsupported there's nothing to do.
  // No need to raise an error since handshake would be done successfully without compression.
  Dbg(dbg_ctl_ssl_cert_compress, "Certificate Compression is unsupported");
  return 1;
#endif
}
