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

#pragma once

#include <string>
#include <set>
#include <vector>

#include <openssl/ssl.h>

#include "tscore/Diags.h"
#include "iocore/net/SSLTypes.h"
#include "tsutil/ts_errata.h"

struct SSLConfigParams;
struct SSLCertLookup;
struct SSLMultiCertConfigParams;
struct SSLLoadingContext;

/**
    @brief Load SSL certificates from ssl_multicert.config and setup SSLCertLookup for SSLCertificateConfig
 */
class SSLMultiCertConfigLoader
{
public:
  struct CertLoadData {
    std::vector<std::string> cert_names_list, key_list, ca_list, ocsp_list;
    std::vector<SSLCertContextType> cert_type_list;
  };
  SSLMultiCertConfigLoader(const SSLConfigParams *p) : _params(p) {}
  virtual ~SSLMultiCertConfigLoader(){};

  swoc::Errata load(SSLCertLookup *lookup);

  virtual SSL_CTX *default_server_ssl_ctx();

  virtual std::vector<SSLLoadingContext> init_server_ssl_ctx(CertLoadData const &data,
                                                             const SSLMultiCertConfigParams *sslMultCertSettings);

  static bool load_certs(SSL_CTX *ctx, const std::vector<std::string> &cert_names_list,
                         const std::vector<std::string> &key_names_list, CertLoadData const &data, const SSLConfigParams *params,
                         const SSLMultiCertConfigParams *sslMultCertSettings);

  bool load_certs_and_cross_reference_names(std::vector<X509 *> &cert_list, CertLoadData &data, const SSLConfigParams *params,
                                            const SSLMultiCertConfigParams *sslMultCertSettings,
                                            std::set<std::string> &common_names,
                                            std::unordered_map<int, std::set<std::string>> &unique_names,
                                            SSLCertContextType *certType);

  static bool set_session_id_context(SSL_CTX *ctx, const SSLConfigParams *params,
                                     const SSLMultiCertConfigParams *sslMultCertSettings);

  static int check_server_cert_now(X509 *cert, const char *certname);
  static void clear_pw_references(SSL_CTX *ssl_ctx);

  bool update_ssl_ctx(const std::string &secret_name);

protected:
  const SSLConfigParams *_params;

  bool _store_single_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams &sslMultCertSettings, shared_SSL_CTX ctx,
                             SSLCertContextType ctx_type, std::set<std::string> &names);

private:
  virtual const char *_debug_tag() const;
  virtual const DbgCtl &_dbg_ctl() const;
  virtual bool _store_ssl_ctx(SSLCertLookup *lookup, shared_SSLMultiCertConfigParams ssl_multi_cert_params);
  bool _prep_ssl_ctx(const shared_SSLMultiCertConfigParams &sslMultCertSettings, SSLMultiCertConfigLoader::CertLoadData &data,
                     std::set<std::string> &common_names, std::unordered_map<int, std::set<std::string>> &unique_names);
  virtual void _set_handshake_callbacks(SSL_CTX *ctx);
  virtual bool _setup_session_cache(SSL_CTX *ctx);
  virtual bool _setup_dialog(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _set_verify_path(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _setup_session_ticket(SSL_CTX *ctx, const SSLMultiCertConfigParams *sslMultCertSettings);
  virtual bool _setup_client_cert_verification(SSL_CTX *ctx);
  virtual bool _set_cipher_suites_for_legacy_versions(SSL_CTX *ctx);
  virtual bool _set_cipher_suites(SSL_CTX *ctx);
  virtual bool _set_curves(SSL_CTX *ctx);
  virtual bool _set_info_callback(SSL_CTX *ctx);
  virtual bool _set_npn_callback(SSL_CTX *ctx);
  virtual bool _set_alpn_callback(SSL_CTX *ctx);
  virtual bool _set_keylog_callback(SSL_CTX *ctx);
  virtual bool _enable_ktls(SSL_CTX *ctx);
  virtual bool _enable_early_data(SSL_CTX *ctx);
};
