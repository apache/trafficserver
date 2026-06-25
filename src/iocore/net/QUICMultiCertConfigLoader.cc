/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "P_SSLCertLookup.h"
#include "P_SSLConfig.h"
#include "iocore/net/QUICMultiCertConfigLoader.h"
#include "iocore/net/TLSSNISupport.h"
#include "iocore/net/quic/QUICConfig.h"
#include "mgmt/config/ConfigContextDiags.h"

#include <string>
#include <string_view>

int QUICCertConfig::_config_id = 0;

//
// QUICCertConfig
//
void
QUICCertConfig::startup()
{
  reconfigure();
}

void
QUICCertConfig::reconfigure(ConfigContext ctx)
{
  bool                     retStatus = true;
  SSLConfig::scoped_config params;
  SSLCertLookup           *lookup = new SSLCertLookup();

  CfgLoadInProgress(ctx, "(quic) %s loading ...", params->configFilePath);

  QUICMultiCertConfigLoader loader(params);
  auto                      errata = loader.load(lookup, _config_id == 0);
  if (!lookup->is_valid || (errata.has_severity() && errata.severity() >= ERRATA_ERROR)) {
    retStatus = false;
  }

  if (retStatus || _config_id == 0) {
    _config_id = configProcessor.set(_config_id, lookup);
  } else {
    delete lookup;
  }

  if (!errata.empty()) {
    ctx.log(errata);
  }

  if (retStatus) {
    CfgLoadComplete(ctx, "(quic) %s finished loading", params->configFilePath);
  } else {
    CfgLoadFail(ctx, "(quic) %s failed to load", params->configFilePath);
  }
}

SSLCertLookup *
QUICCertConfig::acquire()
{
  return static_cast<SSLCertLookup *>(configProcessor.get(_config_id));
}

void
QUICCertConfig::release(SSLCertLookup *lookup)
{
  configProcessor.release(_config_id, lookup);
}

//
// QUICMultiCertConfigLoader
//
SSL_CTX *
QUICMultiCertConfigLoader::default_server_ssl_ctx()
{
  return quic_new_server_ssl_ctx();
}

#if TS_HAS_OPENSSL_QUIC
namespace
{
DbgCtl dbg_ctl_quic{"quic"};
} // end anonymous namespace

namespace
{
void
free_quic_sni_ex_data(void *, void *ptr, CRYPTO_EX_DATA *, int, long, void *)
{
  delete static_cast<std::string *>(ptr);
}

int
quic_sni_ex_data_index()
{
  static int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, free_quic_sni_ex_data);

  return index;
}

void
remember_quic_sni(SSL *ssl, std::string_view servername)
{
  if (ssl == nullptr || servername.empty()) {
    return;
  }

  auto const index = quic_sni_ex_data_index();
  if (index < 0) {
    return;
  }

  auto *stored = static_cast<std::string *>(SSL_get_ex_data(ssl, index));
  if (stored == nullptr) {
    stored = new std::string;
    if (SSL_set_ex_data(ssl, index, stored) != 1) {
      delete stored;
      return;
    }
  }
  stored->assign(servername);

  if (auto *snis = TLSSNISupport::getInstance(ssl); snis != nullptr) {
    snis->set_sni_server_name(servername);
  }
}

bool
apply_quic_sni_certificate(SSL *ssl, SSL_CTX *ctx)
{
  X509     *cert = SSL_CTX_get0_certificate(ctx);
  EVP_PKEY *key  = SSL_CTX_get0_privatekey(ctx);

  if (cert == nullptr && key == nullptr) {
    return true;
  }
  if (cert == nullptr || key == nullptr) {
    return false;
  }

  if (SSL_use_certificate(ssl, cert) != 1 || SSL_use_PrivateKey(ssl, key) != 1) {
    return false;
  }

  STACK_OF(X509) *chain = nullptr;
  if (SSL_CTX_get0_chain_certs(ctx, &chain) == 1 && chain != nullptr && SSL_set1_chain(ssl, chain) != 1) {
    return false;
  }

  return true;
}

bool
select_quic_sni_context(SSL *ssl, std::string_view servername)
{
  if (servername.empty()) {
    return true;
  }

  QUICCertConfig::scoped_config lookup;
  if (!lookup) {
    Dbg(dbg_ctl_quic, "OpenSSL QUIC SNI context selection could not acquire cert lookup");
    return false;
  }

  SSLCertContext *cert_context = lookup->find(std::string(servername));
  if (cert_context == nullptr) {
    return true;
  }

  shared_SSL_CTX const selected_ctx = cert_context->getCtx();
  if (selected_ctx == nullptr) {
    Dbg(dbg_ctl_quic, "OpenSSL QUIC SNI context selection found null context for SNI '%.*s'", static_cast<int>(servername.size()),
        servername.data());
    return false;
  }

  SSL_set_SSL_CTX(ssl, selected_ctx.get());
  if (!apply_quic_sni_certificate(ssl, selected_ctx.get())) {
    Dbg(dbg_ctl_quic, "OpenSSL QUIC SNI context selection failed to apply SSL_CTX %p certificate to ssl=%p", selected_ctx.get(),
        ssl);
    return false;
  }

  return true;
}

std::string_view
servername_from_client_hello(SSL *ssl)
{
  unsigned char const *p         = nullptr;
  size_t               remaining = 0;
  if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &p, &remaining) != 1 || remaining <= 2) {
    return {};
  }

  size_t len  = (static_cast<size_t>(*p++) << 8);
  len        += *p++;
  if (len + 2 != remaining) {
    return {};
  }
  remaining = len;

  if (remaining <= 3 || *p++ != TLSEXT_NAMETYPE_host_name) {
    return {};
  }
  --remaining;

  len  = (static_cast<size_t>(*p++) << 8);
  len += *p++;
  if (len + 2 > remaining) {
    return {};
  }

  return {reinterpret_cast<char const *>(p), len};
}

int
quic_client_hello_callback(SSL *ssl, int *, void *)
{
  auto const servername = servername_from_client_hello(ssl);
  remember_quic_sni(ssl, servername);

  return select_quic_sni_context(ssl, servername) ? SSL_CLIENT_HELLO_SUCCESS : SSL_CLIENT_HELLO_ERROR;
}

int
quic_servername_callback(SSL *ssl, int *, void *)
{
  char const *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  auto const  sni        = servername == nullptr ? std::string_view{} : std::string_view{servername};
  remember_quic_sni(ssl, sni);

  return select_quic_sni_context(ssl, sni) ? SSL_TLSEXT_ERR_OK : SSL_TLSEXT_ERR_ALERT_FATAL;
}
} // end anonymous namespace

std::string_view
quic_sni_server_name(SSL *ssl)
{
  if (ssl == nullptr) {
    return {};
  }

  auto const index = quic_sni_ex_data_index();
  if (index < 0) {
    return {};
  }

  auto *stored = static_cast<std::string *>(SSL_get_ex_data(ssl, index));

  return stored == nullptr ? std::string_view{} : std::string_view{*stored};
}

void
QUICMultiCertConfigLoader::_set_handshake_callbacks(SSL_CTX *ctx)
{
  Dbg(dbg_ctl_quic, "installing OpenSSL QUIC cert callback on SSL_CTX %p", ctx);
  SSL_CTX_set_client_hello_cb(ctx, quic_client_hello_callback, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ctx, quic_servername_callback);
  SSL_CTX_set_cert_cb(
    ctx,
    [](SSL *ssl, void *) -> int {
      char const *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
      auto const  sni        = servername == nullptr ? std::string_view{} : std::string_view{servername};
      remember_quic_sni(ssl, sni);

      return select_quic_sni_context(ssl, sni) ? 1 : 0;
    },
    nullptr);
}

namespace
{
int
quic_alpn_select_callback(SSL * /* ssl ATS_UNUSED */, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                          unsigned int inlen, void * /* arg ATS_UNUSED */)
{
  static constexpr unsigned char server_protos[] = {2, 'h', '3', 5, 'h', '3', '-', '2', '9', 5, 'h', '3', '-', '2', '7'};

  if (SSL_select_next_proto(const_cast<unsigned char **>(out), outlen, server_protos, sizeof(server_protos), in, inlen) ==
      OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_OK;
  }

  *out    = nullptr;
  *outlen = 0;
  return SSL_TLSEXT_ERR_ALERT_FATAL;
}
} // end anonymous namespace

bool
QUICMultiCertConfigLoader::_set_alpn_callback(SSL_CTX *ctx)
{
  SSL_CTX_set_alpn_select_cb(ctx, quic_alpn_select_callback, nullptr);
  return true;
}

bool
QUICMultiCertConfigLoader::_set_curves(SSL_CTX *ctx)
{
  QUICConfig::scoped_config params;
  if (params->server_supported_groups() == nullptr) {
    return true;
  }

  if (!SSL_CTX_set1_groups_list(ctx, params->server_supported_groups())) {
    Error("invalid QUIC groups list for server in records.config");
    return false;
  }

  return true;
}
#endif

bool
QUICMultiCertConfigLoader::_setup_session_cache(SSL_CTX *ctx)
{
#if TS_HAS_OPENSSL_QUIC
  SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
#else
  (void)ctx;
#endif

  // Disabled for now
  // TODO Check if the logic in SSLMultiCertConfigLoader is reusable
  return true;
}

bool
QUICMultiCertConfigLoader::_set_cipher_suites_for_legacy_versions(SSL_CTX * /* ctx ATS_UNUSED */)
{
  // Do not set this since QUIC only uses TLS 1.3
  return true;
}

bool
QUICMultiCertConfigLoader::_set_info_callback(SSL_CTX * /* ctx ATS_UNUSED */)
{
  // Disabled for now
  // TODO Check if we need this for QUIC
  return true;
}

bool
QUICMultiCertConfigLoader::_set_npn_callback(SSL_CTX * /* ctx ATS_UNUSED */)
{
  // Do not set a callback for NPN since QUIC doesn't use it
  return true;
}

const char *
QUICMultiCertConfigLoader::_debug_tag() const
{
  return "quic";
}
