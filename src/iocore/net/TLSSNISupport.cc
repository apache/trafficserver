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
#include "P_SSLNextProtocolAccept.h"
#include "iocore/net/SSLSNIConfig.h"
#include "iocore/net/TLSSNISupport.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"
#include "tscore/ink_inet.h"
#include "tscore/Diags.h"

int TLSSNISupport::_ex_data_index = -1;

namespace
{

DbgCtl dbg_ctl_ssl_sni{"ssl_sni"};

} // end anonymous namespace

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
TLSSNISupport::perform_sni_action(SSL &ssl)
{
  const char *servername = this->get_sni_server_name();
  if (!servername) {
    Dbg(dbg_ctl_ssl_sni, "No servername provided");
    return SSL_TLSEXT_ERR_OK;
  }

  SNIConfig::scoped_config params;
  auto const               port{this->_get_local_port()};
  if (auto const &actions = params->get({servername, std::strlen(servername)}, port); !actions.first) {
    Dbg(dbg_ctl_ssl_sni, "%s:%i not available in the map", servername, port);
  } else {
    for (auto &&item : *actions.first) {
      auto ret = item->SNIAction(ssl, actions.second);
      if (ret != SSL_TLSEXT_ERR_OK) {
        return ret;
      }
    }
  }
  return SSL_TLSEXT_ERR_OK;
}

void
TLSSNISupport::on_client_hello(ClientHello &client_hello)
{
  const char          *servername = nullptr;
  const unsigned char *p;
  size_t               remaining, len;
  // Parse the server name if the get extension call succeeds and there are more than 2 bytes to parse
  if (client_hello.getExtension(TLSEXT_TYPE_server_name, &p, &remaining) && remaining > 2) {
    // Parse to get to the name, originally from test/handshake_helper.c in openssl tree
    /* Extract the length of the supplied list of names. */
    len  = *(p++) << 8;
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
          len  = *(p++) << 8;
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

void
TLSSNISupport::on_servername(SSL *ssl, int * /* al ATS_UNUSED */, void * /* arg ATS_UNUSED */)
{
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
TLSSNISupport::get_sni_server_name() const
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

// See if any of the client-side actions would trigger for this combination of servername and client IP
// host_sni_policy is an in/out parameter.  It starts with the global policy from the records.yaml
// setting proxy.config.http.host_sni_policy and is possibly overridden if the sni policy
// contains a host_sni_policy entry
bool
TLSSNISupport::would_have_actions_for(const char *servername, IpEndpoint remote, int &enforcement_policy)
{
  bool                     retval = false;
  SNIConfig::scoped_config params;

  auto const &actions = params->get(servername, this->_get_local_port());
  if (actions.first) {
    for (auto &&item : *actions.first) {
      retval |= item->TestClientSNIAction(servername, remote, enforcement_policy);
    }
  }
  return retval;
}

int
TLSSNISupport::ClientHello::getExtension(int type, const uint8_t **out, size_t *outlen)
{
#if HAVE_SSL_CTX_SET_CLIENT_HELLO_CB
  return SSL_client_hello_get0_ext(this->_chc, type, out, outlen);
#elif HAVE_SSL_CTX_SET_SELECT_CERTIFICATE_CB
  return SSL_early_callback_ctx_extension_get(this->_chc, type, out, outlen);
#endif
}
