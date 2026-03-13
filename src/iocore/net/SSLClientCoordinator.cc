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
#include "iocore/net/SSLSNIConfig.h"
#include "mgmt/config/ConfigRegistry.h"
#include "tscore/Filenames.h"
#if TS_USE_QUIC == 1
#include "iocore/net/QUICMultiCertConfigLoader.h"
#endif

void
SSLClientCoordinator::reconfigure(ConfigContext reconf_ctx)
{
  // The SSLConfig must have its configuration loaded before the SNIConfig.
  // The SSLConfig owns the client cert context storage and the SNIConfig will load
  // into it.
  SSLConfig::reconfigure(reconf_ctx.add_dependent_ctx("SSLConfig"));
  SNIConfig::reconfigure(reconf_ctx.add_dependent_ctx("SNIConfig"));
  SSLCertificateConfig::reconfigure(reconf_ctx.add_dependent_ctx("SSLCertificateConfig"));
#if TS_USE_QUIC == 1
  QUICCertConfig::reconfigure(reconf_ctx.add_dependent_ctx("QUICCertConfig"));
#endif
  reconf_ctx.complete("SSL configs reloaded");
}

void
SSLClientCoordinator::startup()
{
  // Register with ConfigRegistry — no primary file, this is a pure coordinator.
  // File dependencies (sni.yaml, ssl_multicert.config) are tracked via add_file_and_node_dependency
  // so (when enabled) the RPC handler can route injected YAML content to the coordinator's handler.
  config::ConfigRegistry::Get_Instance().register_record_config(
    "ssl_client_coordinator",                                          // registry key
    [](ConfigContext ctx) { SSLClientCoordinator::reconfigure(ctx); }, // reload handler
    {"proxy.config.ssl.client.cert.path",                              // trigger records
     "proxy.config.ssl.client.cert.filename", "proxy.config.ssl.client.private_key.path",
     "proxy.config.ssl.client.private_key.filename", "proxy.config.ssl.keylog_file", "proxy.config.ssl.server.cert.path",
     "proxy.config.ssl.server.private_key.path", "proxy.config.ssl.server.cert_chain.filename",
     "proxy.config.ssl.server.session_ticket.enable"});

  // Track sni.yaml — FileManager watches for mtime changes, record wired to trigger reload.
  // When enabled, the "sni" dep_key makes this routable for RPC inline content.
  config::ConfigRegistry::Get_Instance().add_file_and_node_dependency(
    "ssl_client_coordinator", "sni", "proxy.config.ssl.servername.filename", ts::filename::SNI, false);

  // Track ssl_multicert.config — same pattern.
  config::ConfigRegistry::Get_Instance().add_file_and_node_dependency(
    "ssl_client_coordinator", "ssl_multicert", "proxy.config.ssl.server.multicert.filename", ts::filename::SSL_MULTICERT, false);

  // Sub-module initialization (order matters: SSLConfig before SNIConfig)
  SSLConfig::startup();
  SNIConfig::startup();
}
