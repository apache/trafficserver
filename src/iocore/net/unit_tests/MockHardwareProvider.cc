/** @file

  A minimal OpenSSL 3 provider for unit tests, standing in for an HSM or other
  hardware-backed key store.

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

#include "MockHardwareProvider.h"

#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/core_object.h>
#include <openssl/params.h>
#include <openssl/store.h>

#include <cstring>
#include <string>

namespace MockHardwareProvider
{

char const SCHEME[]{"hwtest"};
char const URI[]{"hwtest:server-key"};

namespace
{

  // The key material a load of URI yields, PEM-encoded. Owned by ScopedProvider,
  // which is not copyable and is intended for use by one test at a time.
  std::string key_pem;

  struct LoaderCtx {
    bool exhausted{false};
  };

  void *
  store_open(void * /* provctx */, char const *uri)
  {
    if (std::strncmp(uri, SCHEME, sizeof(SCHEME) - 1) != 0) {
      return nullptr;
    }
    return new LoaderCtx{};
  }

  int
  store_load(void *loaderctx, OSSL_CALLBACK *object_cb, void *object_cbarg, OSSL_PASSPHRASE_CALLBACK * /* pw_cb */,
             void * /* pw_cbarg */)
  {
    auto *ctx = static_cast<LoaderCtx *>(loaderctx);

    if (ctx->exhausted) {
      return 0;
    }
    ctx->exhausted = true;

    // Report the key as an unparsed PEM blob and let the default provider's
    // decoders turn it into an EVP_PKEY, which is how a store loader with no
    // opinion about encoding hands back an object.
    int        object_type{OSSL_OBJECT_PKEY};
    OSSL_PARAM params[3];

    params[0] = OSSL_PARAM_construct_int(OSSL_OBJECT_PARAM_TYPE, &object_type);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_OBJECT_PARAM_DATA, key_pem.data(), key_pem.size());
    params[2] = OSSL_PARAM_construct_end();
    return object_cb(params, object_cbarg);
  }

  int
  store_eof(void *loaderctx)
  {
    return static_cast<LoaderCtx *>(loaderctx)->exhausted ? 1 : 0;
  }

  int
  store_close(void *loaderctx)
  {
    delete static_cast<LoaderCtx *>(loaderctx);
    return 1;
  }

  // OSSL_STORE sets OSSL_STORE_PARAM_EXPECT when the caller wants one specific
  // object type. This loader only ever yields a private key, so the hint is
  // accepted and ignored.
  OSSL_PARAM const *
  store_settable_ctx_params(void * /* provctx */)
  {
    static OSSL_PARAM const known[]{
      OSSL_PARAM_int(OSSL_STORE_PARAM_EXPECT, nullptr),
      OSSL_PARAM_END,
    };
    return known;
  }

  int
  store_set_ctx_params(void * /* loaderctx */, OSSL_PARAM const /* params */[])
  {
    return 1;
  }

  OSSL_DISPATCH const store_functions[]{
    {OSSL_FUNC_STORE_OPEN,                reinterpret_cast<void (*)()>(store_open)               },
    {OSSL_FUNC_STORE_LOAD,                reinterpret_cast<void (*)()>(store_load)               },
    {OSSL_FUNC_STORE_EOF,                 reinterpret_cast<void (*)()>(store_eof)                },
    {OSSL_FUNC_STORE_CLOSE,               reinterpret_cast<void (*)()>(store_close)              },
    {OSSL_FUNC_STORE_SETTABLE_CTX_PARAMS, reinterpret_cast<void (*)()>(store_settable_ctx_params)},
    {OSSL_FUNC_STORE_SET_CTX_PARAMS,      reinterpret_cast<void (*)()>(store_set_ctx_params)     },
    {0,                                   nullptr                                                },
  };

  OSSL_ALGORITHM const store_algorithms[]{
    {SCHEME,  "provider=hwtest", store_functions, "mock hardware key store"},
    {nullptr, nullptr,           nullptr,         nullptr                  },
  };

  OSSL_ALGORITHM const *
  query_operation(void * /* provctx */, int operation_id, int *no_cache)
  {
    *no_cache = 0;
    return operation_id == OSSL_OP_STORE ? store_algorithms : nullptr;
  }

  OSSL_DISPATCH const provider_functions[]{
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)()>(query_operation)},
    {0,                                  nullptr                                      },
  };

  int
  provider_init(OSSL_CORE_HANDLE const *handle, OSSL_DISPATCH const * /* in */, OSSL_DISPATCH const **out, void **provctx)
  {
    *provctx = const_cast<OSSL_CORE_HANDLE *>(handle);
    *out     = provider_functions;
    return 1;
  }

} // namespace

ScopedProvider::ScopedProvider(std::string const &pem)
{
  key_pem = pem;
  if (1 != OSSL_PROVIDER_add_builtin(nullptr, SCHEME, provider_init)) {
    return;
  }
  // Activating any provider replaces the implicit default, and the decoders
  // that turn this provider's PEM blob into an EVP_PKEY live in the default
  // provider, so load it explicitly alongside.
  this->default_provider = OSSL_PROVIDER_load(nullptr, "default");
  this->hw_provider      = OSSL_PROVIDER_load(nullptr, SCHEME);
}

ScopedProvider::~ScopedProvider()
{
  if (this->hw_provider != nullptr) {
    OSSL_PROVIDER_unload(this->hw_provider);
  }
  if (this->default_provider != nullptr) {
    OSSL_PROVIDER_unload(this->default_provider);
  }
  key_pem.clear();
}

bool
ScopedProvider::is_loaded() const
{
  return this->default_provider != nullptr && this->hw_provider != nullptr;
}

} // namespace MockHardwareProvider
