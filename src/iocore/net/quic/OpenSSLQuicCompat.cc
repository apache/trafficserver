/** @file

  Bridges quiche's legacy QUIC TLS callback API to OpenSSL 3.5's third-party
  QUIC TLS callback API.

  This compatibility layer exports the quictls/BoringSSL-style symbols that
  quiche expects while ATS links against upstream OpenSSL 3.5. It stores
  per-SSL callback state in OpenSSL ex-data and translates CRYPTO data,
  encryption secrets, alerts, and transport parameters between the two APIs.

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
  agreements.  See the NOTICE file distributed with this work for additional information regarding
  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the License.  You may obtain
  a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include <openssl/core_dispatch.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

enum ssl_encryption_level_t {
  ssl_encryption_initial = 0,
  ssl_encryption_early_data,
  ssl_encryption_handshake,
  ssl_encryption_application,
};

struct ssl_quic_method_st {
  int (*set_encryption_secrets)(SSL *ssl, ssl_encryption_level_t level, uint8_t const *read_secret, uint8_t const *write_secret,
                                size_t secret_len);
  int (*add_handshake_data)(SSL *ssl, ssl_encryption_level_t level, uint8_t const *data, size_t len);
  int (*flush_flight)(SSL *ssl);
  int (*send_alert)(SSL *ssl, ssl_encryption_level_t level, uint8_t alert);
};

using SSL_QUIC_METHOD = ssl_quic_method_st;

namespace
{

constexpr auto read_secret_direction  = 0;
constexpr auto write_secret_direction = 1;
constexpr auto quic_level_count       = 4;

struct PendingSecret {
  std::vector<uint8_t> read;
  std::vector<uint8_t> write;
  bool                 have_read{false};
  bool                 have_write{false};
  bool                 delivered{false};
};

struct QuicCompatState {
  SSL_QUIC_METHOD const                                         *method{nullptr};
  std::array<std::deque<std::vector<uint8_t>>, quic_level_count> crypto_data;
  std::array<PendingSecret, quic_level_count>                    secrets;
  ssl_encryption_level_t                                         read_level{ssl_encryption_initial};
  ssl_encryption_level_t                                         write_level{ssl_encryption_initial};
  ssl_encryption_level_t                                         active_recv_level{ssl_encryption_initial};
  bool                                                           active_recv{false};
  std::vector<uint8_t>                                           local_transport_params;
  std::vector<uint8_t>                                           peer_transport_params;
};

void
free_quic_ex_data(void *, void *ptr, CRYPTO_EX_DATA *, int, long, void *)
{
  delete static_cast<QuicCompatState *>(ptr);
}

int
quic_ex_data_index()
{
  static int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, free_quic_ex_data);

  return index;
}

bool
is_valid_level(ssl_encryption_level_t level)
{
  auto index = static_cast<int>(level);

  return index >= 0 && index < quic_level_count;
}

size_t
level_index(ssl_encryption_level_t level)
{
  return static_cast<size_t>(level);
}

uint8_t const *
data_or_null(std::vector<uint8_t> const &data)
{
  return data.empty() ? nullptr : data.data();
}

bool
assign_bytes(std::vector<uint8_t> &dst, uint8_t const *data, size_t len)
{
  if (len == 0) {
    dst.clear();
    return true;
  }
  if (data == nullptr) {
    return false;
  }

  dst.assign(data, data + len);
  return true;
}

QuicCompatState *
get_state(SSL const *ssl)
{
  if (ssl == nullptr) {
    return nullptr;
  }

  int const index = quic_ex_data_index();
  return index < 0 ? nullptr : static_cast<QuicCompatState *>(SSL_get_ex_data(ssl, index));
}

QuicCompatState *
get_or_create_state(SSL *ssl)
{
  if (ssl == nullptr) {
    return nullptr;
  }

  if (auto *state = get_state(ssl); state != nullptr) {
    return state;
  }

  auto     *state = new QuicCompatState;
  int const index = quic_ex_data_index();
  if (index < 0 || SSL_set_ex_data(ssl, index, state) != 1) {
    delete state;
    return nullptr;
  }

  return state;
}

bool
compat_level_from_protection(uint32_t prot_level, ssl_encryption_level_t &level)
{
  switch (prot_level) {
  case OSSL_RECORD_PROTECTION_LEVEL_NONE:
    level = ssl_encryption_initial;
    return true;
  case OSSL_RECORD_PROTECTION_LEVEL_EARLY:
    level = ssl_encryption_early_data;
    return true;
  case OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE:
    level = ssl_encryption_handshake;
    return true;
  case OSSL_RECORD_PROTECTION_LEVEL_APPLICATION:
    level = ssl_encryption_application;
    return true;
  default:
    return false;
  }
}

bool
deliver_0rtt_secret(SSL *ssl, QuicCompatState &state, ssl_encryption_level_t level, int direction,
                    std::vector<uint8_t> const &secret)
{
  if (state.method == nullptr || state.method->set_encryption_secrets == nullptr) {
    return false;
  }

  bool const is_server = SSL_is_server(ssl) == 1;
  if (direction == read_secret_direction && is_server) {
    return state.method->set_encryption_secrets(ssl, level, data_or_null(secret), nullptr, secret.size()) == 1;
  }

  if (direction == write_secret_direction && !is_server) {
    return state.method->set_encryption_secrets(ssl, level, nullptr, data_or_null(secret), secret.size()) == 1;
  }

  return true;
}

bool
deliver_paired_secrets(SSL *ssl, QuicCompatState &state, ssl_encryption_level_t level)
{
  if (state.method == nullptr || state.method->set_encryption_secrets == nullptr) {
    return false;
  }

  auto &pending = state.secrets[level_index(level)];
  if (!pending.have_read || !pending.have_write || pending.delivered) {
    return true;
  }
  if (pending.read.size() != pending.write.size()) {
    return false;
  }

  int const result =
    state.method->set_encryption_secrets(ssl, level, data_or_null(pending.read), data_or_null(pending.write), pending.read.size());
  if (result == 1) {
    pending.delivered = true;
  }

  return result == 1;
}

int
crypto_send_cb(SSL *ssl, unsigned char const *buf, size_t buf_len, size_t *consumed, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr || state->method == nullptr || state->method->add_handshake_data == nullptr) {
    return 0;
  }

  if (consumed != nullptr) {
    *consumed = 0;
  }

  static constexpr uint8_t empty_data = 0;
  if (buf == nullptr && buf_len > 0) {
    return 0;
  }

  auto const *data = buf_len == 0 ? &empty_data : reinterpret_cast<uint8_t const *>(buf);
  if (state->method->add_handshake_data(ssl, state->write_level, data, buf_len) != 1) {
    return 0;
  }
  if (state->method->flush_flight != nullptr && state->method->flush_flight(ssl) != 1) {
    return 0;
  }

  if (consumed != nullptr) {
    *consumed = buf_len;
  }

  return 1;
}

int
crypto_recv_rcd_cb(SSL *ssl, unsigned char const **buf, size_t *bytes_read, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr || buf == nullptr || bytes_read == nullptr) {
    return 0;
  }

  *buf        = nullptr;
  *bytes_read = 0;

  if (state->active_recv) {
    auto &active_queue = state->crypto_data[level_index(state->active_recv_level)];
    if (!active_queue.empty()) {
      *buf        = active_queue.front().data();
      *bytes_read = active_queue.front().size();
    }
    return 1;
  }

  auto &queue = state->crypto_data[level_index(state->read_level)];
  if (queue.empty()) {
    return 1;
  }

  state->active_recv       = true;
  state->active_recv_level = state->read_level;
  *buf                     = queue.front().data();
  *bytes_read              = queue.front().size();

  return 1;
}

int
crypto_release_rcd_cb(SSL *ssl, size_t bytes_read, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr || !state->active_recv) {
    return 1;
  }

  auto &queue = state->crypto_data[level_index(state->active_recv_level)];
  if (!queue.empty()) {
    if (bytes_read >= queue.front().size()) {
      queue.pop_front();
    } else {
      queue.front().erase(queue.front().begin(), queue.front().begin() + bytes_read);
    }
  }
  state->active_recv = false;

  return 1;
}

int
yield_secret_cb(SSL *ssl, uint32_t prot_level, int direction, unsigned char const *secret, size_t secret_len, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr) {
    return 0;
  }

  ssl_encryption_level_t level = ssl_encryption_initial;
  if (!compat_level_from_protection(prot_level, level)) {
    return 0;
  }

  if (direction == read_secret_direction) {
    state->read_level = level;
  } else if (direction == write_secret_direction) {
    state->write_level = level;
  } else {
    return 0;
  }

  std::vector<uint8_t> secret_copy;
  if (!assign_bytes(secret_copy, reinterpret_cast<uint8_t const *>(secret), secret_len)) {
    return 0;
  }

  if (level == ssl_encryption_early_data) {
    return deliver_0rtt_secret(ssl, *state, level, direction, secret_copy) ? 1 : 0;
  }

  auto &pending = state->secrets[level_index(level)];
  if (direction == read_secret_direction) {
    pending.read      = std::move(secret_copy);
    pending.have_read = true;
  } else {
    pending.write      = std::move(secret_copy);
    pending.have_write = true;
  }
  pending.delivered = false;

  return deliver_paired_secrets(ssl, *state, level) ? 1 : 0;
}

int
got_transport_params_cb(SSL *ssl, unsigned char const *params, size_t params_len, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr) {
    return 0;
  }

  if (!assign_bytes(state->peer_transport_params, reinterpret_cast<uint8_t const *>(params), params_len)) {
    return 0;
  }

  return 1;
}

int
alert_cb(SSL *ssl, unsigned char alert_code, void *)
{
  auto *state = get_state(ssl);
  if (state == nullptr || state->method == nullptr || state->method->send_alert == nullptr) {
    return 0;
  }

  return state->method->send_alert(ssl, state->write_level, alert_code);
}

OSSL_DISPATCH const quic_tls_callbacks[] = {
  {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_SEND,          reinterpret_cast<void (*)(void)>(crypto_send_cb)         },
  {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RECV_RCD,      reinterpret_cast<void (*)(void)>(crypto_recv_rcd_cb)     },
  {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RELEASE_RCD,   reinterpret_cast<void (*)(void)>(crypto_release_rcd_cb)  },
  {OSSL_FUNC_SSL_QUIC_TLS_YIELD_SECRET,         reinterpret_cast<void (*)(void)>(yield_secret_cb)        },
  {OSSL_FUNC_SSL_QUIC_TLS_GOT_TRANSPORT_PARAMS, reinterpret_cast<void (*)(void)>(got_transport_params_cb)},
  {OSSL_FUNC_SSL_QUIC_TLS_ALERT,                reinterpret_cast<void (*)(void)>(alert_cb)               },
  {0,                                           nullptr                                                  },
};

} // namespace

extern "C" int
SSL_set_quic_method(SSL *ssl, SSL_QUIC_METHOD const *quic_method)
{
  auto *state = get_or_create_state(ssl);
  if (state == nullptr) {
    return 0;
  }

  state->method = quic_method;

  return SSL_set_quic_tls_cbs(ssl, quic_tls_callbacks, nullptr);
}

extern "C" int
SSL_set_quic_transport_params(SSL *ssl, uint8_t const *params, size_t params_len)
{
  auto *state = get_or_create_state(ssl);
  if (state == nullptr) {
    return 0;
  }

  if (!assign_bytes(state->local_transport_params, params, params_len)) {
    return 0;
  }
  return SSL_set_quic_tls_transport_params(ssl, data_or_null(state->local_transport_params), state->local_transport_params.size());
}

extern "C" void
SSL_get_peer_quic_transport_params(SSL const *ssl, uint8_t const **out_params, size_t *out_params_len)
{
  auto const *state = get_state(ssl);
  if (out_params != nullptr) {
    *out_params = state == nullptr ? nullptr : data_or_null(state->peer_transport_params);
  }
  if (out_params_len != nullptr) {
    *out_params_len = state == nullptr ? 0 : state->peer_transport_params.size();
  }
}

extern "C" ssl_encryption_level_t
SSL_quic_write_level(SSL const *ssl)
{
  auto const *state = get_state(ssl);

  return state == nullptr ? ssl_encryption_initial : state->write_level;
}

extern "C" int
SSL_provide_quic_data(SSL *ssl, ssl_encryption_level_t level, uint8_t const *data, size_t len)
{
  auto *state = get_or_create_state(ssl);
  if (state == nullptr || !is_valid_level(level)) {
    return 0;
  }

  auto &records = state->crypto_data[level_index(level)];
  records.emplace_back();
  if (!assign_bytes(records.back(), data, len)) {
    records.pop_back();
    return 0;
  }

  return 1;
}

extern "C" int
SSL_process_quic_post_handshake(SSL *ssl)
{
  auto *state = get_state(ssl);
  if (state == nullptr) {
    return 0;
  }

  bool has_crypto_data = false;
  for (auto const &queue : state->crypto_data) {
    if (!queue.empty()) {
      has_crypto_data = true;
      break;
    }
  }
  if (!has_crypto_data) {
    return 1;
  }

  unsigned char data       = 0;
  size_t        bytes_read = 0;
  int const     result     = SSL_read_ex(ssl, &data, 0, &bytes_read);

  if (result == 1) {
    return 1;
  }

  int const error = SSL_get_error(ssl, result);

  return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
}

extern "C" void
SSL_set_quic_use_legacy_codepoint(SSL *, int)
{
}
