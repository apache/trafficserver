/** @file

  TLSSNISupport implements common methods and members to
  support protocols for Server Name Indication

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

#include <optional>
#include <string_view>
#include <memory>
#include <openssl/ssl.h>
#include "tscore/ink_config.h"

class TLSSNISupport
{
public:
  virtual ~TLSSNISupport() = default;

  static void initialize();
  static TLSSNISupport *getInstance(SSL *ssl);
  static void bind(SSL *ssl, TLSSNISupport *snis);
  static void unbind(SSL *ssl);

  int perform_sni_action();
  // Callback functions for OpenSSL libraries
#if TS_USE_HELLO_CB || defined(OPENSSL_IS_BORINGSSL)
#ifdef OPENSSL_IS_BORINGSSL
  void on_client_hello(const SSL_CLIENT_HELLO *client_hello);
#else
  void on_client_hello(SSL *ssl, int *al, void *arg);
#endif
#endif
  void on_servername(SSL *ssl, int *al, void *arg);

  struct HintsFromSNI {
    std::optional<uint32_t> http2_buffer_water_mark;
    std::optional<uint32_t> http2_max_settings_frames_per_minute;
    std::optional<uint32_t> http2_max_ping_frames_per_minute;
    std::optional<uint32_t> http2_max_priority_frames_per_minute;
    std::optional<uint32_t> http2_max_rst_stream_frames_per_minute;
  } hints_from_sni;

protected:
  virtual void _fire_ssl_servername_event() = 0;

  void _clear();
  const char *_get_sni_server_name() const;

private:
  static int _ex_data_index;

  // Null-terminated string, or nullptr if there is no SNI server name.
  std::unique_ptr<char[]> _sni_server_name;

  void _set_sni_server_name(std::string_view name);
};
