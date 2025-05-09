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

#include "tscore/ink_memory.h"
#include "SSLTypes.h"

#include <netinet/in.h>
#include <openssl/ssl.h>

#include <string_view>
#include <memory>
#include <optional>

class TLSSNISupport
{
public:
  class ClientHello
  {
  public:
    ClientHello(ClientHelloContainer chc) : _chc(chc) {}
    /**
     * @return 1 if successful
     */
    int getExtension(int type, const uint8_t **out, size_t *outlen);

  private:
    ClientHelloContainer _chc;
  };

  virtual ~TLSSNISupport() = default;

  static void           initialize();
  static TLSSNISupport *getInstance(SSL *ssl);
  static void           bind(SSL *ssl, TLSSNISupport *snis);
  static void           unbind(SSL *ssl);

  int perform_sni_action(SSL &ssl);
  // Callback functions for OpenSSL libraries

  /** Process a CLIENT_HELLO from a client.
   *
   * This is for client-side connections.
   */
  void on_client_hello(ClientHello &client_hello);

  /** Process the servername extension when a client uses one in the TLS handshake.
   *
   * This is for client-side connections.
   */
  void on_servername(SSL *ssl, int *al, void *arg);

  /** Set the servername extension for server-side connections.
   *
   * This is for server-side connections.
   * This calls SSL_set_tlsext_host_name() to set the servername extension.
   *
   * @param ssl The SSL object upon which the servername extension is set.
   * @param name The servername to set. This is assumed to be a non-empty,
   *   null-terminated string.
   * @return True if the servername was set successfully, false otherwise.
   */
  bool set_sni_server_name(SSL *ssl, char const *name);

  /**
   * Get the server name in SNI
   *
   * @return Either a pointer to the (null-terminated) name fetched from the TLS object or the empty string.
   */
  const char *get_sni_server_name() const;
  bool        would_have_actions_for(const char *servername, IpEndpoint remote, int &enforcement_policy);

  struct HintsFromSNI {
    std::optional<uint32_t>         http2_buffer_water_mark;
    std::optional<uint32_t>         server_max_early_data;
    std::optional<uint32_t>         http2_initial_window_size_in;
    std::optional<int32_t>          http2_max_settings_frames_per_minute;
    std::optional<int32_t>          http2_max_ping_frames_per_minute;
    std::optional<int32_t>          http2_max_priority_frames_per_minute;
    std::optional<int32_t>          http2_max_rst_stream_frames_per_minute;
    std::optional<int32_t>          http2_max_continuation_frames_per_minute;
    std::optional<std::string_view> outbound_sni_policy;
  } hints_from_sni;

protected:
  virtual in_port_t _get_local_port() = 0;

  void _clear();

private:
  static int _ex_data_index;

  // Null-terminated string, or nullptr if there is no SNI server name.
  std::unique_ptr<char[]> _sni_server_name;

  void _set_sni_server_name_buffer(std::string_view name);
};
