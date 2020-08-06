/**

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

#pragma once

#ifndef OPENSSL_IS_BORINGSSL
#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#endif
#include <openssl/ssl.h>

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "P_SSLCertLookup.h"

struct SSLConfigParams;

/**
    @brief Load SSL certificates from ssl_multicert.config and setup SSLCertLookup for SSLCertificateConfig
 */
class SSLMultiCertConfigLoader
{
public:
  struct CertLoadData {
    std::vector<std::string> cert_names_list, key_list, ca_list, ocsp_list;
  };
  SSLMultiCertConfigLoader(const SSLConfigParams *p) : _params(p) {}
  virtual ~SSLMultiCertConfigLoader(){};

  bool load(SSLCertLookup *lookup);

  virtual SSL_CTX *default_server_ssl_ctx();
  virtual SSL_CTX *init_server_ssl_ctx(CertLoadData const &data, const SSLMultiCertConfigParams *sslMultCertSettings,
                                       std::set<std::string> &names);

  static bool load_certs(SSL_CTX *ctx, CertLoadData const &data, const SSLConfigParams *params,
                         const SSLMultiCertConfigParams *sslMultCertSettings);
  bool load_certs_and_cross_reference_names(std::vector<X509 *> &cert_list, CertLoadData &data, const SSLConfigParams *params,
                                            const SSLMultiCertConfigParams *sslMultCertSettings,
                                            std::set<std::string> &common_names,
                                            std::unordered_map<int, std::set<std::string>> &unique_names);
  static bool set_session_id_context(SSL_CTX *ctx, const SSLConfigParams *params,
                                     const SSLMultiCertConfigParams *sslMultCertSettings);

  static bool index_certificate(SSLCertLookup *lookup, SSLCertContext const &cc, const char *sni_name);
  static int check_server_cert_now(X509 *cert, const char *certname);
  static void clear_pw_references(SSL_CTX *ssl_ctx);

protected:
  const SSLConfigParams *_params;

  bool _store_single_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams &sslMultCertSettings, shared_SSL_CTX ctx,
                             std::set<std::string> &names);

private:
  virtual const char *_debug_tag() const;
  bool _store_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams &ssl_multi_cert_params);
  virtual void _set_handshake_callbacks(SSL_CTX *ctx);
};
