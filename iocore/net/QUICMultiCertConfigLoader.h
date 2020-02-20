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

#include "P_SSLCertLookup.h"
#include "P_SSLUtils.h"

class QUICCertConfig
{
public:
  static void startup();
  static void reconfigure();
  static SSLCertLookup *acquire();
  static void release(SSLCertLookup *lookup);

  using scoped_config = ConfigProcessor::scoped_config<QUICCertConfig, SSLCertLookup>;

private:
  static int _config_id;
};

class QUICMultiCertConfigLoader : public SSLMultiCertConfigLoader
{
public:
  QUICMultiCertConfigLoader(const SSLConfigParams *p) : SSLMultiCertConfigLoader(p) {}

  virtual SSL_CTX *default_server_ssl_ctx() override;
  // virtual SSL_CTX *init_server_ssl_ctx(std::vector<X509 *> &cert_list, const SSLMultiCertConfigParams *multi_cert_params)
  // override;
  SSL_CTX *init_server_ssl_ctx(std::vector<std::string> const &cert_names_list, std::vector<std::string> const &key_list,
                               std::vector<std::string> const &ca_list, std::vector<std::string> &ocsp_list,
                               const SSLMultiCertConfigParams *sslMultCertSettings, std::set<std::string> &names) override;

private:
  // virtual SSL_CTX *_store_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams multi_cert_params) override;
  bool _store_ssl_ctx(SSLCertLookup *lookup, const shared_SSLMultiCertConfigParams ssl_multi_cert_params) override;
  virtual void _set_handshake_callbacks(SSL_CTX *ssl_ctx) override;
  static int ssl_select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                                      unsigned inlen, void *);
  static int ssl_cert_cb(SSL *ssl, void *arg);
  static int ssl_sni_cb(SSL *ssl, int *ad, void *arg);
};
