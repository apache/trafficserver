/** @file

  SSLClientCoordinator.cc - Coordinate the loading of SSL related configs

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

#include "P_SSLClientCoordinator.h"
#include "P_SSLConfig.h"
#include "P_SSLSNI.h"

std::unique_ptr<ConfigUpdateHandler<SSLClientCoordinator>> sslClientUpdate;

void
SSLClientCoordinator::reconfigure()
{
  // The SSLConfig must have its configuration loaded before the SNIConfig.
  // The SSLConfig owns the client cert context storage and the SNIConfig will load
  // into it.
  SSLConfig::reconfigure();
  SNIConfig::reconfigure();
  SSLCertificateConfig::reconfigure();
}

void
SSLClientCoordinator::startup()
{
  // The SSLConfig must have its configuration loaded before the SNIConfig.
  // The SSLConfig owns the client cert context storage and the SNIConfig will load
  // into it.
  sslClientUpdate.reset(new ConfigUpdateHandler<SSLClientCoordinator>());
  sslClientUpdate->attach("proxy.config.ssl.client.cert.path");
  sslClientUpdate->attach("proxy.config.ssl.client.cert.filename");
  sslClientUpdate->attach("proxy.config.ssl.client.private_key.path");
  sslClientUpdate->attach("proxy.config.ssl.client.private_key.filename");
  sslClientUpdate->attach("proxy.config.ssl.keylog_file");
  SSLConfig::startup();
  sslClientUpdate->attach("proxy.config.ssl.servername.filename");
  SNIConfig::startup();
  sslClientUpdate->attach("proxy.config.ssl.server.multicert.filename");
  sslClientUpdate->attach("proxy.config.ssl.server.cert.path");
  sslClientUpdate->attach("proxy.config.ssl.server.private_key.path");
  sslClientUpdate->attach("proxy.config.ssl.server.cert_chain.filename");
  sslClientUpdate->attach("proxy.config.ssl.server.session_ticket.enable");
}
