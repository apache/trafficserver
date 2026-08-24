#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

function(CHECK_OPENSSL_HAS_QUIC_TLS_CBS OUT_VAR OPENSSL_INCLUDE_DIR)
  set(CHECK_PROGRAM
      "
        #include <openssl/ssl.h>
        #include <openssl/core_dispatch.h>

        int main() {
            OSSL_FUNC_SSL_QUIC_TLS_crypto_send_fn *crypto_send = nullptr;
            OSSL_FUNC_SSL_QUIC_TLS_crypto_recv_rcd_fn *crypto_recv = nullptr;
            OSSL_FUNC_SSL_QUIC_TLS_crypto_release_rcd_fn *crypto_release = nullptr;
            OSSL_FUNC_SSL_QUIC_TLS_yield_secret_fn *yield_secret = nullptr;
            OSSL_FUNC_SSL_QUIC_TLS_got_transport_params_fn *got_transport_params = nullptr;
            OSSL_FUNC_SSL_QUIC_TLS_alert_fn *alert = nullptr;
            const OSSL_DISPATCH callbacks[] = {
                {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_SEND, reinterpret_cast<void (*)(void)>(crypto_send)},
                {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RECV_RCD, reinterpret_cast<void (*)(void)>(crypto_recv)},
                {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RELEASE_RCD, reinterpret_cast<void (*)(void)>(crypto_release)},
                {OSSL_FUNC_SSL_QUIC_TLS_YIELD_SECRET, reinterpret_cast<void (*)(void)>(yield_secret)},
                {OSSL_FUNC_SSL_QUIC_TLS_GOT_TRANSPORT_PARAMS, reinterpret_cast<void (*)(void)>(got_transport_params)},
                {OSSL_FUNC_SSL_QUIC_TLS_ALERT, reinterpret_cast<void (*)(void)>(alert)},
                {0, nullptr},
            };

            return callbacks[0].function_id == 0 ||
                   SSL_set_quic_tls_cbs == nullptr ||
                   SSL_set_quic_tls_transport_params == nullptr;
        }
        "
  )
  set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("${CHECK_PROGRAM}" ${OUT_VAR})
  set(${OUT_VAR}
      ${${OUT_VAR}}
      PARENT_SCOPE
  )
endfunction()
