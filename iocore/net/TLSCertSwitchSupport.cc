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

#include "TLSCertSwitchSupport.h"
#include "P_SSLCertLookup.h"

int TLSCertSwitchSupport::_ex_data_index = -1;

void
TLSCertSwitchSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSEarlyDataSupport index", nullptr, nullptr, nullptr);
  }
}

TLSCertSwitchSupport *
TLSCertSwitchSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSCertSwitchSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSCertSwitchSupport::bind(SSL *ssl, TLSCertSwitchSupport *tcss)
{
  SSL_set_ex_data(ssl, _ex_data_index, tcss);
}

void
TLSCertSwitchSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
TLSCertSwitchSupport::_clear()
{
}

int
TLSCertSwitchSupport::selectCertificate(SSL *ssl, SSLCertContextType ctxType)
{
  shared_SSL_CTX ctx = nullptr;

  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  bool found             = true;

  Debug("ssl", "set_context_cert ssl=%p server=%s", ssl, servername);

  // catch the client renegotiation early on
  if (this->_isTryingRenegotiation()) {
    Debug("ssl_load", "set_context_cert trying to renegotiate from the client");
    return 0;
  }

  // The incoming SSL_CTX is either the one mapped from the inbound IP address or the default one. If we
  // don't find a name-based match at this point, we *do not* want to mess with the context because we've
  // already made a best effort to find the best match.
  if (likely(servername)) {
    ctx = this->_lookupContextByName(servername, ctxType);
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == nullptr) {
    ctx = this->_lookupContextByIP();
  }

  if (ctx != nullptr) {
    SSL_set_SSL_CTX(ssl, ctx.get());
  } else {
    found = false;
  }

  SSL_CTX *verify_ctx = SSL_get_SSL_CTX(ssl);
  // set_context_cert found SSL context for ...
  Debug("ssl_load", "ssl_cert_callback %s SSL context %p for requested name '%s'", found ? "found" : "using", verify_ctx,
        servername);

  if (verify_ctx == nullptr) {
    return 0;
  }

  return 1;
}
