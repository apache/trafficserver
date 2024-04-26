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

#include "iocore/net/SSLMultiCertConfigLoader.h"
#include "iocore/eventsystem/ConfigProcessor.h"

class QUICCertConfig
{
public:
  static void           startup();
  static void           reconfigure();
  static SSLCertLookup *acquire();
  static void           release(SSLCertLookup *lookup);

  using scoped_config = ConfigProcessor::scoped_config<QUICCertConfig, SSLCertLookup>;

private:
  static int _config_id;
};

class QUICMultiCertConfigLoader : public SSLMultiCertConfigLoader
{
public:
  QUICMultiCertConfigLoader(const SSLConfigParams *p) : SSLMultiCertConfigLoader(p) {}

  virtual SSL_CTX *default_server_ssl_ctx() override;

private:
  const char  *_debug_tag() const override;
  virtual bool _setup_session_cache(SSL_CTX *ctx) override;
  virtual bool _set_cipher_suites_for_legacy_versions(SSL_CTX *ctx) override;
  virtual bool _set_info_callback(SSL_CTX *ctx) override;
  virtual bool _set_npn_callback(SSL_CTX *ctx) override;
};
