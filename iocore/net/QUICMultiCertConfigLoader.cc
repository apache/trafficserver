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

#include "QUICMultiCertConfigLoader.h"
#include "P_SSLConfig.h"
#include "P_SSLNextProtocolSet.h"
#include "P_OCSPStapling.h"
#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICConnection.h"
#include "QUICTypes.h"
#include "tscore/Filenames.h"
// #include "QUICGlobals.h"

#define QUICConfDebug(fmt, ...) Debug("quic_conf", fmt, ##__VA_ARGS__)
#define QUICGlobalQCDebug(qc, fmt, ...) Debug("quic_global", "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

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
QUICCertConfig::reconfigure()
{
  SSLConfig::scoped_config params;
  SSLCertLookup *lookup = new SSLCertLookup();

  QUICMultiCertConfigLoader loader(params);
  loader.load(lookup);

  _config_id = configProcessor.set(_config_id, lookup);
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
  return quic_new_ssl_ctx();
}

SSL_CTX *
QUICMultiCertConfigLoader::init_server_ssl_ctx(std::vector<std::string> const &cert_names_list,
                                               std::vector<std::string> const &key_list, std::vector<std::string> const &ca_list,
                                               std::vector<std::string> &ocsp_list,
                                               const SSLMultiCertConfigParams *multi_cert_params, std::set<std::string> &names)

{
  const SSLConfigParams *params = this->_params;

  SSL_CTX *ctx = this->default_server_ssl_ctx();

  if (multi_cert_params) {
    if (multi_cert_params->dialog) {
      // TODO: dialog support
    }

    if (multi_cert_params->cert) {
      if (!SSLMultiCertConfigLoader::load_certs(ctx, cert_names_list, key_list, ca_list, ocsp_list, params, multi_cert_params)) {
        goto fail;
      }
    }

    // SSL_CTX_load_verify_locations() builds the cert chain from the
    // serverCACertFilename if that is not nullptr.  Otherwise, it uses the hashed
    // symlinks in serverCACertPath.
    //
    // if ssl_ca_name is NOT configured for this cert in ssl_multicert.config
    //     AND
    // if proxy.config.ssl.CA.cert.filename and proxy.config.ssl.CA.cert.path
    //     are configured
    //   pass that file as the chain (include all certs in that file)
    // else if proxy.config.ssl.CA.cert.path is configured (and
    //       proxy.config.ssl.CA.cert.filename is nullptr)
    //   use the hashed symlinks in that directory to build the chain
    if (!multi_cert_params->ca && params->serverCACertPath != nullptr) {
      if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(ctx))) {
        Error("invalid CA Certificate file or CA Certificate path");
        goto fail;
      }
    }
  }

  if (params->clientCertLevel != 0) {
    // TODO: client cert support
  }

  if (!SSLMultiCertConfigLoader::set_session_id_context(ctx, params, multi_cert_params)) {
    goto fail;
  }

#if TS_USE_TLS_SET_CIPHERSUITES
  if (params->server_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(ctx, params->server_tls13_cipher_suites)) {
      Error("invalid tls server cipher suites in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

#if defined(SSL_CTX_set1_groups_list) || defined(SSL_CTX_set1_curves_list)
  if (params->server_groups_list != nullptr) {
#ifdef SSL_CTX_set1_groups_list
    if (!SSL_CTX_set1_groups_list(ctx, params->server_groups_list)) {
#else
    if (!SSL_CTX_set1_curves_list(ctx, params->server_groups_list)) {
#endif
      Error("invalid groups list for server in %s", ts::filename::RECORDS);
      goto fail;
    }
  }
#endif

  // SSL_CTX_set_info_callback(ctx, ssl_callback_info);

  SSL_CTX_set_alpn_select_cb(ctx, QUICMultiCertConfigLoader::ssl_select_next_protocol, nullptr);

  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(ctx, true);
  }

  return ctx;

fail:
  SSLReleaseContext(ctx);
  return nullptr;
}

bool
QUICMultiCertConfigLoader::_store_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams multi_cert_params)
{
  bool retval = false;
  std::vector<X509 *> cert_list;
  std::set<std::string> common_names;
  std::map<int, std::set<std::string>> unique_names;
  std::vector<std::string> cert_name_list, key_list, ca_list, ocsp_list;
  const SSLConfigParams *params = this->_params;
  this->load_certs_and_cross_reference_names(cert_list, cert_name_list, key_list, ca_list, ocsp_list, params,
                                             multi_cert_params.get(), common_names, unique_names);

  int i = 0;
  for (auto cert : cert_list) {
    const char *current_cert_name = cert_name_list[i].c_str();
    if (0 > SSLMultiCertConfigLoader::check_server_cert_now(cert, current_cert_name)) {
      /* At this point, we know cert is bad, and we've already printed a
         descriptive reason as to why cert is bad to the log file */
      QUICConfDebug("Marking certificate as NOT VALID: %s", current_cert_name);
      lookup->is_valid = false;
    }
    i++;
  }

  shared_SSL_CTX ctx(this->init_server_ssl_ctx(cert_name_list, key_list, ca_list, ocsp_list, multi_cert_params.get(), common_names),
                     SSL_CTX_free);

  shared_ssl_ticket_key_block keyblock = nullptr;

  if (!ctx || !multi_cert_params || !this->_store_single_ssl_ctx(lookup, multi_cert_params, ctx, common_names)) {
    lookup->is_valid = false;
    retval           = false;
  }

  auto iter = unique_names.begin();
  for (; retval && iter != unique_names.end(); ++iter) {
    size_t i = iter->first;

    std::vector<std::string> single_cert_name_list, single_key_list, single_ca_list, single_ocsp_list;
    single_cert_name_list.push_back(cert_name_list[i]);
    single_key_list.push_back(i < key_list.size() ? key_list[i] : "");
    single_ca_list.push_back(i < ca_list.size() ? ca_list[i] : "");
    single_ocsp_list.push_back(i < ocsp_list.size() ? ocsp_list[i] : "");

    shared_SSL_CTX unique_ctx(this->init_server_ssl_ctx(single_cert_name_list, single_key_list, single_ca_list, single_ocsp_list,
                                                        multi_cert_params.get(), iter->second),
                              SSL_CTX_free);
    if (!unique_ctx || !this->_store_single_ssl_ctx(lookup, multi_cert_params, unique_ctx, iter->second)) {
      lookup->is_valid = false;
      retval           = false;
    }
  }

  for (auto &i : cert_list) {
    X509_free(i);
  }

  return retval;
}

void
QUICMultiCertConfigLoader::_set_handshake_callbacks(SSL_CTX *ssl_ctx)
{
  SSL_CTX_set_cert_cb(ssl_ctx, QUICMultiCertConfigLoader::ssl_cert_cb, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ssl_ctx, QUICMultiCertConfigLoader::ssl_sni_cb);

  // Set client hello callback if needed
  // SSL_CTX_set_client_hello_cb(ctx, QUIC::ssl_client_hello_cb, nullptr);
}

int
QUICMultiCertConfigLoader::ssl_select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                                                    const unsigned char *in, unsigned inlen, void *)
{
  QUICConnection *qc = static_cast<QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));

  if (qc) {
    return qc->select_next_protocol(ssl, out, outlen, in, inlen);
  }
  return SSL_TLSEXT_ERR_NOACK;
}

int
QUICMultiCertConfigLoader::ssl_sni_cb(SSL *ssl, int * /*ad*/, void * /*arg*/)
{
  // XXX: add SNIConfig support ?
  // XXX: add TRANSPORT_BLIND_TUNNEL support ?
  return 1;
}

int
QUICMultiCertConfigLoader::ssl_cert_cb(SSL *ssl, void * /*arg*/)
{
  shared_SSL_CTX ctx = nullptr;
  SSLCertContext *cc = nullptr;
  QUICCertConfig::scoped_config lookup;
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  QUICConnection *qc     = static_cast<QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));

  if (servername == nullptr) {
    servername = "";
  }
  QUICGlobalQCDebug(qc, "SNI=%s", servername);

  // The incoming SSL_CTX is either the one mapped from the inbound IP address or the default one. If we
  // don't find a name-based match at this point, we *do not* want to mess with the context because we've
  // already made a best effort to find the best match.
  if (likely(servername)) {
    cc = lookup->find(const_cast<char *>(servername));
    if (cc && cc->getCtx()) {
      ctx = cc->getCtx();
    }
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == nullptr) {
    QUICFiveTuple five_tuple = qc->five_tuple();
    IpEndpoint ip            = five_tuple.destination();
    cc                       = lookup->find(ip);

    if (cc && cc->getCtx()) {
      ctx = cc->getCtx();
    }
  }

  bool found = true;
  if (ctx != nullptr) {
    SSL_set_SSL_CTX(ssl, ctx.get());
  } else {
    found = false;
  }

  SSL_CTX *verify_ctx = nullptr;
  verify_ctx          = SSL_get_SSL_CTX(ssl);
  QUICGlobalQCDebug(qc, "%s SSL_CTX %p for requested name '%s'", found ? "found" : "using", verify_ctx, servername);

  if (verify_ctx == nullptr) {
    return 0;
  }

  return 1;
}
