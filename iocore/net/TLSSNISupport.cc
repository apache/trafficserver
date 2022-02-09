/** @file

  SNISupport.cc provides implementations for SNISupport methods

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
#include "TLSSNISupport.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"
#include "P_SSLSNI.h"

int TLSSNISupport::_ex_data_index = -1;

void
TLSSNISupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSSNISupport index", nullptr, nullptr, nullptr);
  }
}

TLSSNISupport *
TLSSNISupport::getInstance(SSL *ssl)
{
  return static_cast<TLSSNISupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSSNISupport::bind(SSL *ssl, TLSSNISupport *snis)
{
  SSL_set_ex_data(ssl, _ex_data_index, snis);
}

void
TLSSNISupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

int
TLSSNISupport::perform_sni_action()
{
  const char *servername = this->_get_sni_server_name();
  if (!servername) {
    Debug("ssl_sni", "No servername provided");
    return SSL_TLSEXT_ERR_OK;
  }

  SNIConfig::scoped_config params;
  if (const auto &actions = params->get({servername, std::strlen(servername)}); !actions.first) {
    Debug("ssl_sni", "%s not available in the map", servername);
  } else {
    for (auto &&item : *actions.first) {
      auto ret = item->SNIAction(this, actions.second);
      if (ret != SSL_TLSEXT_ERR_OK) {
        return ret;
      }
    }
  }
  return SSL_TLSEXT_ERR_OK;
}

#if TS_USE_HELLO_CB || defined(OPENSSL_IS_BORINGSSL)
void
#ifdef OPENSSL_IS_BORINGSSL
TLSSNISupport::on_client_hello(const SSL_CLIENT_HELLO *client_hello)
#else
TLSSNISupport::on_client_hello(SSL *ssl, int *al, void *arg)
#endif
{
  const char *servername = nullptr;
  const unsigned char *p;
  size_t remaining, len;
  // Parse the server name if the get extension call succeeds and there are more than 2 bytes to parse
#ifdef OPENSSL_IS_BORINGSSL
  if (SSL_early_callback_ctx_extension_get(client_hello, TLSEXT_TYPE_server_name, &p, &remaining) && remaining > 2)
#else
  if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &p, &remaining) && remaining > 2)
#endif
  {
    // Parse to get to the name, originally from test/handshake_helper.c in openssl tree
    /* Extract the length of the supplied list of names. */
    len = *(p++) << 8;
    len += *(p++);
    if (len + 2 == remaining) {
      remaining = len;
      /*
       * The list in practice only has a single element, so we only consider
       * the first one.
       */
      if (*p++ == TLSEXT_NAMETYPE_host_name) {
        remaining--;
        /* Now we can finally pull out the byte array with the actual hostname. */
        if (remaining > 2) {
          len = *(p++) << 8;
          len += *(p++);
          if (len + 2 <= remaining) {
            servername = reinterpret_cast<const char *>(p);
          }
        }
      }
    }
  }
  if (servername) {
    this->_set_sni_server_name(std::string_view(servername, len));
  }
}
#endif

void
TLSSNISupport::on_servername(SSL *ssl, int *al, void *arg)
{
  this->_fire_ssl_servername_event();

  const char *name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (name) {
    this->_set_sni_server_name(name);
  }
}

void
TLSSNISupport::_clear()
{
  _sni_server_name.reset();
}

const char *
TLSSNISupport::_get_sni_server_name() const
{
  return _sni_server_name.get() ? _sni_server_name.get() : "";
}

void
TLSSNISupport::_set_sni_server_name(std::string_view name)
{
  if (name.size()) {
    char *n = new char[name.size() + 1];
    std::memcpy(n, name.data(), name.size());
    n[name.size()] = '\0';
    _sni_server_name.reset(n);
  }
}
