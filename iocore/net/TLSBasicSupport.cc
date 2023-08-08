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
  this->_tls_handshake_begin_time = 0;
  this->_tls_handshake_end_time   = 0;
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
