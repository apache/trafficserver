/** @file

  A brief file description

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

#pragma once

#include <openssl/ssl.h>
#include <memory>
#include "tscore/ink_config.h"

enum class SNIRoutingType {
  NONE = 0,
  BLIND,
  FORWARD,
  PARTIAL_BLIND,
};

/** Used to discern the context type when BoringSSL is used for the SSL implementation.
 */
enum class SSLCertContextType {
  GENERIC, ///< Generic Context (can be either EC or RSA)
  RSA,     ///< RSA-based Context
  EC       ///< EC-based Context
};

#ifndef OPENSSL_IS_BORINGSSL
using ssl_curve_id = int;
#else
using ssl_curve_id = uint16_t;
#endif

#if HAVE_SSL_CTX_SET_CLIENT_HELLO_CB
using ClientHelloContainer         = SSL *;
constexpr int CLIENT_HELLO_ERROR   = SSL_CLIENT_HELLO_ERROR;
constexpr int CLIENT_HELLO_RETRY   = SSL_CLIENT_HELLO_RETRY;
constexpr int CLIENT_HELLO_SUCCESS = SSL_CLIENT_HELLO_SUCCESS;
#elif HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB
using ClientHelloContainer                              = const SSL_CLIENT_HELLO *;
constexpr ssl_select_cert_result_t CLIENT_HELLO_ERROR   = ssl_select_cert_error;
constexpr ssl_select_cert_result_t CLIENT_HELLO_RETRY   = ssl_select_cert_retry;
constexpr ssl_select_cert_result_t CLIENT_HELLO_SUCCESS = ssl_select_cert_success;
#endif

struct SSLMultiCertConfigParams;

using shared_SSLMultiCertConfigParams = std::shared_ptr<SSLMultiCertConfigParams>;
using shared_SSL_CTX                  = std::shared_ptr<SSL_CTX>;
