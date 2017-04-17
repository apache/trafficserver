/** @file

  A brief file description

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

/*
 *   replace-protoset.c:
 *	an example plugin...
 * Clones protoset attached with all the accept objects
 * Unregisters H2 from the clone
 * Replaces the protoset attached with all the incoming VCs with a clone
 */
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/PluginInit.h>
#include <ts/ts.h>
#include <ts/TsBuffer.h>

#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <algorithm>
#include <cinttypes>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLNAME "TLS Protocol Adjuster"
#define PLTAG "replace_protoset"

typedef std::unordered_map<int, TSNextProtocolSet> protoTable; // stores protocolset keyed by NetAccept ID
protoTable ProtoSetTable;
typedef std::unordered_set<std::string> Table;
// Map of domains to tweak.
Table _table;

int
CB_SNI(TSCont contp, TSEvent, void *cb_data)
{
  TSVConn vc               = (static_cast<TSVConn>(cb_data));
  TSSslConnection ssl_conn = TSVConnSSLConnectionGet(vc);
  auto *ssl                = reinterpret_cast<SSL *>(ssl_conn);
  char const *sni          = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (sni) {
    if (_table.find(sni) != _table.end()) {
      TSAcceptor na        = TSAcceptorGet(vc);
      int nid              = TSAcceptorIDGet(na);
      TSNextProtocolSet ps = ProtoSetTable[nid];
      TSRegisterProtocolSet(vc, ps);
    }
  }

  TSVConnReenable(vc);
  return TS_SUCCESS;
}

int
CB_NetAcceptReady(TSCont contp, TSEvent event, void *cb_data)
{
  switch (event) {
  case TS_EVENT_LIFECYCLE_PORTS_READY:
    for (int i = 0, totalNA = TSAcceptorCount(); i < totalNA; ++i) {
      TSAcceptor netaccept = TSAcceptorGetbyID(i);
      // get a clone of the protoset associated with the netaccept
      TSNextProtocolSet nps = TSGetcloneProtoSet(netaccept);
      TSUnregisterProtocol(nps, TS_ALPN_PROTOCOL_HTTP_2_0);
      ProtoSetTable[i] = nps;
    }
    break;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, char const *argv[])
{
  int ret = -999, i;
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLNAME;
  info.vendor_name   = "Yahoo!";
  info.support_email = "persia@yahoo-inc.com";
  ret                = TSPluginRegister(&info);

  if (ret != TS_SUCCESS) {
    TSError("Plugin registration failed.");
    return;
  } else {
    if (argc < 2) {
      TSError("[%s] Usage %s servername1 servername2 .... ", PLTAG, PLTAG);
      return;
    }
    TSDebug(PLTAG, "Plugin registration succeeded.");
  }

  for (i = 1; i < argc; i++) {
    TSDebug(PLTAG, "%s added to the No-H2 list", argv[i]);
    _table.emplace(std::string(argv[i], strlen(argv[i])));
  }
  // This should not modify any state so no lock is needed.
  TSCont cb_sni    = TSContCreate(&CB_SNI, nullptr);
  TSCont cb_netacc = TSContCreate(&CB_NetAcceptReady, nullptr);

  TSHttpHookAdd(TS_SSL_SERVERNAME_HOOK, cb_sni);
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, cb_netacc);
}
