/** @file

  TLSSBasicSupport.cc provides implementations for
  TLSBasicSupport methods

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

#include "TLSBasicSupport.h"
#include "SSLStats.h"

#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <memory>

int TLSBasicSupport::_ex_data_index = -1;

void
TLSBasicSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSBasicSupport index", nullptr, nullptr, nullptr);
  }
}

TLSBasicSupport *
TLSBasicSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSBasicSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSBasicSupport::bind(SSL *ssl, TLSBasicSupport *srs)
{
  SSL_set_ex_data(ssl, _ex_data_index, srs);
}

void
TLSBasicSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
TLSBasicSupport::clear()
{
  this->set_verify_cert_store(nullptr);
  this->_tls_handshake_begin_time = 0;
  this->_tls_handshake_end_time   = 0;
}

void
TLSBasicSupport::set_verify_cert_store(X509_STORE *store)
{
  if (this->_verify_cert_store == store) {
    return;
  }

  if (store != nullptr) {
    X509_STORE_up_ref(store);
  }
  if (this->_verify_cert_store != nullptr) {
    X509_STORE_free(this->_verify_cert_store);
  }
  this->_verify_cert_store = store;
}

bool
TLSBasicSupport::is_peer_cert_trusted_by_current_ca() const
{
  SSL *ssl = this->_get_ssl_object();

  if (ssl == nullptr) {
    return false;
  }

  // Without a verify_client_ca_certs override for this SNI, the context's own store is what the
  // handshake would have used.
  X509_STORE *store = this->_verify_cert_store != nullptr ? this->_verify_cert_store : SSL_CTX_get_cert_store(SSL_get_SSL_CTX(ssl));

  if (store == nullptr) {
    return false;
  }

  // SSL_get_peer_certificate is what the rest of this branch uses (see
  // peer_provided_cert() and the handshake paths); it is available on every TLS
  // library 9.2.x supports, including BoringSSL, which has no
  // SSL_get1_peer_certificate.
  std::unique_ptr<X509, decltype(&X509_free)> peer_cert{SSL_get_peer_certificate(ssl), &X509_free};

  if (peer_cert == nullptr) {
    return false;
  }

  std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> store_ctx{X509_STORE_CTX_new(), &X509_STORE_CTX_free};

  if (store_ctx == nullptr) {
    return false;
  }

  STACK_OF(X509) *chain = SSL_get_peer_cert_chain(ssl);

  if (X509_STORE_CTX_init(store_ctx.get(), store, peer_cert.get(), chain) != 1) {
    return false;
  }

  // Match what the TLS library itself applies to a client certificate during a full handshake.
  if (X509_STORE_CTX_set_default(store_ctx.get(), "ssl_client") != 1 ||
      X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx.get()), SSL_get0_param(ssl)) != 1) {
    return false;
  }

  return X509_verify_cert(store_ctx.get()) == 1;
}

const char *
TLSBasicSupport::get_tls_protocol_name() const
{
  auto ssl = this->_get_ssl_object();
  if (ssl) {
    return SSL_get_version(ssl);
  } else {
    return nullptr;
  }
}

const char *
TLSBasicSupport::get_tls_cipher_suite() const
{
  auto ssl = this->_get_ssl_object();
  if (ssl) {
    return SSL_get_cipher_name(ssl);
  } else {
    return nullptr;
  }
}

const char *
TLSBasicSupport::get_tls_curve() const
{
  auto ssl = this->_get_ssl_object();

  if (!ssl) {
    return nullptr;
  }
  ssl_curve_id curve = this->_get_tls_curve();
#ifndef OPENSSL_IS_BORINGSSL
  if (curve == NID_undef) {
    return nullptr;
  }
  return OBJ_nid2sn(curve);
#else
  if (curve == 0) {
    return nullptr;
  }
  return SSL_get_curve_name(curve);
#endif
}

ink_hrtime
TLSBasicSupport::get_tls_handshake_begin_time() const
{
  return this->_tls_handshake_begin_time;
}

ink_hrtime
TLSBasicSupport::get_tls_handshake_end_time() const
{
  return this->_tls_handshake_end_time;
}

void
TLSBasicSupport::_record_tls_handshake_begin_time()
{
  this->_tls_handshake_begin_time = ink_get_hrtime();
}

void
TLSBasicSupport::_record_tls_handshake_end_time()
{
  this->_tls_handshake_end_time       = ink_get_hrtime();
  const ink_hrtime ssl_handshake_time = this->_tls_handshake_end_time - this->_tls_handshake_begin_time;

  Debug("ssl", "ssl handshake time:%" PRId64, ssl_handshake_time);
  SSL_INCREMENT_DYN_STAT_EX(ssl_total_handshake_time_stat, ssl_handshake_time);
}
