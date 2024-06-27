/** @file

  TLSSEarlyDataSupport.cc provides implementations for
  TLSEarlyDataSupport methods

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

#include <openssl/ssl.h>
#include "iocore/net/TLSEarlyDataSupport.h"
#include "tscore/ink_config.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"

int TLSEarlyDataSupport::_ex_data_index = -1;

void
TLSEarlyDataSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSEarlyDataSupport index", nullptr, nullptr, nullptr);
  }
}

TLSEarlyDataSupport *
TLSEarlyDataSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSEarlyDataSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSEarlyDataSupport::bind(SSL *ssl, TLSEarlyDataSupport *srs)
{
  SSL_set_ex_data(ssl, _ex_data_index, srs);
}

void
TLSEarlyDataSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
TLSEarlyDataSupport::clear()
{
  this->_early_data_len = 0;
}

size_t
TLSEarlyDataSupport::get_early_data_len() const
{
  return this->_early_data_len;
}

void
TLSEarlyDataSupport::update_early_data_config([[maybe_unused]] SSL *ssl, [[maybe_unused]] uint32_t max_early_data,
                                              [[maybe_unused]] uint32_t recv_max_early_data)
{
#if TS_HAS_TLS_EARLY_DATA
  // Must disable OpenSSL's internal anti-replay if external cache is used with
  // 0-rtt, otherwise session reuse will be broken. The freshness check described
  // in https://tools.ietf.org/html/rfc8446#section-8.3 is still performed. But we
  // still need to implement something to try to prevent replay atacks.
  //
  // We are now also disabling this when using OpenSSL's internal cache, since we
  // are calling "ssl_accept" non-blocking, it seems to be confusing the anti-replay
  // mechanism and causing session resumption to fail.
#ifdef HAVE_SSL_SET_MAX_EARLY_DATA
  static DbgCtl dbg_ctl_ssl_early_data{"ssl_early_data"};
  bool          ret1 = false;
  bool          ret2 = false;
  if ((ret1 = SSL_set_max_early_data(ssl, max_early_data)) == 1) {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_max_early_data %u: success", max_early_data);
  } else {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_max_early_data %u: failed", max_early_data);
  }

  if ((ret2 = SSL_set_recv_max_early_data(ssl, recv_max_early_data)) == 1) {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_recv_max_early_data %u: success", recv_max_early_data);
  } else {
    Dbg(dbg_ctl_ssl_early_data, "SSL_set_recv_max_early_data %u: failed", recv_max_early_data);
  }

  if (ret1 && ret2) {
    Dbg(dbg_ctl_ssl_early_data, "Must disable anti-replay if 0-rtt is enabled.");
    SSL_set_options(ssl, SSL_OP_NO_ANTI_REPLAY);
  }
#else
  // If SSL_set_max_early_data is unavailable, it's probably BoringSSL,
  // and SSL_set_early_data_enabled should be available.
  SSL_set_early_data_enabled(ssl, max_early_data > 0 ? 1 : 0);
  Warning("max_early_data is not used due to library limitations");
#endif
#endif
}

void
TLSEarlyDataSupport::_increment_early_data_len(size_t amount)
{
  this->_early_data_len += amount;
}
