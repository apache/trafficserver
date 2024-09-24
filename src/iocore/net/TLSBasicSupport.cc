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

#include "iocore/net/TLSBasicSupport.h"
#include "SSLStats.h"

int TLSBasicSupport::_ex_data_index = -1;

namespace
{
DbgCtl dbg_ctl_ssl{"ssl"};

} // end anonymous namespace

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

TLSHandle
TLSBasicSupport::get_tls_handle() const
{
  return this->_get_ssl_object();
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
#if !HAVE_SSL_GET_CURVE_NAME
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
TLSBasicSupport::set_valid_tls_protocols(unsigned long proto_mask, unsigned long max_mask)
{
  auto ssl = this->_get_ssl_object();
  SSL_set_options(ssl, proto_mask);
  SSL_clear_options(ssl, max_mask & ~proto_mask);
}

void
TLSBasicSupport::set_valid_tls_version_min(int min)
{
  auto ssl = this->_get_ssl_object();

  // Ignore available versions set by SSL_(CTX_)set_options if a ragne is specified
  SSL_clear_options(ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);

  int ver = 0;
  if (min >= 0) {
    ver = TLS1_VERSION + min;
  }
  SSL_set_min_proto_version(ssl, ver);
}

void
TLSBasicSupport::set_valid_tls_version_max(int max)
{
  auto ssl = this->_get_ssl_object();
  // Ignore available versions set by SSL_(CTX_)set_options if a ragne is specified
  SSL_clear_options(ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);

  int ver = 0;
  if (max >= 0) {
    ver = TLS1_VERSION + max;
  }
  SSL_set_max_proto_version(ssl, ver);
}

int
TLSBasicSupport::verify_certificate(X509_STORE_CTX *ctx)
{
  // See comments in SSLNetVConnection::_verify_certificate
  this->_cert_to_verify = ctx;
  int ret               = this->_verify_certificate(ctx);
  this->_cert_to_verify = nullptr;

  return ret;
}

X509_STORE_CTX *
TLSBasicSupport::get_tls_cert_to_verify() const
{
  return this->_cert_to_verify;
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

  Dbg(dbg_ctl_ssl, "ssl handshake time:%" PRId64, ssl_handshake_time);
  Metrics::Counter::increment(ssl_rsb.total_handshake_time, ssl_handshake_time);
}
