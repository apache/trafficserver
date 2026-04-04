/** @file

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

#include "ts/ts.h"

#include "context.h"
#include <string_view>

/** @brief Handler invoked on TLS ClientHello for connection-based fingerprinting. */
using ClientHelloHandler = void (*)(JAxContext *, TSVConn);

/** @brief Handler invoked on HTTP request read for request-based fingerprinting. */
using RequestReadHandler = void (*)(JAxContext *, TSHttpTxn);
using VConnCloseHandler  = void (*)(TSVConn);

/**
 * @brief Describes a fingerprinting method (e.g. JA3, JA4, JA4H).
 *
 * Each method has a type that determines when it runs:
 *  - @c CONNECTION_BASED methods fire on the TLS ClientHello via @c on_client_hello.
 *  - @c REQUEST_BASED methods fire on the HTTP request via @c on_request.
 *
 * Concrete method instances are defined in per-method directories
 * (ja3/, ja4/, ja4h/) and selected by the plugin configuration.
 */
struct Method {
  enum class Type {
    /** Fingerprint derived from TLS ClientHello. */
    CONNECTION_BASED,
    /** Fingerprint derived from the HTTP request. */
    REQUEST_BASED,
  };

  /**
   * Human-readable method name (e.g. "ja3").
   * Must reference a null-terminated string.
   */
  std::string_view name;

  /** When this method runs. */
  Type type;

  /** Callback for CONNECTION_BASED methods (may be nullptr). */
  ClientHelloHandler on_client_hello;

  /** Callback for REQUEST_BASED methods (may be nullptr). */
  RequestReadHandler on_request;
  VConnCloseHandler  on_vconn_close;
};
