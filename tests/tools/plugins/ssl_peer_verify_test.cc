/** @file

  Peer verification test plugin.

  Used in Uranium tests to verify TS_SSL_PEER_VERIFY_CLIENT_HOOK and
  TS_SSL_PEER_VERIFY_SERVER_HOOK for both X.509 and RPK peer credentials.

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

#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <ts/ts.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>

#define PLUGIN_NAME "ssl_peer_verify_test"

namespace
{
DbgCtl dbg_ctl{"ssl_verify"};

bool opt_client       = false;
bool opt_server       = false;
bool opt_reject_rpk   = false;
bool opt_reject_x509  = false;
bool opt_require_rpk  = false;
bool opt_require_x509 = false;

int
CB_peer_verify(TSCont /* contp */, TSEvent event, void *edata)
{
  TSSslPeerVerifyCTX verify_ctx = static_cast<TSSslPeerVerifyCTX>(edata);
  TSVConn            ssl_vc     = TSSslPeerVerifyCTXVConnGet(verify_ctx);
  TSSslPeerCredType  cred_type  = TSSslPeerVerifyCTXCredTypeGet(verify_ctx);
  int                preverify  = TSSslPeerVerifyCTXPreverifyGet(verify_ctx);
  const char        *hook_name  = (event == TS_EVENT_SSL_PEER_VERIFY_CLIENT) ? "CLIENT" : "SERVER";

  Dbg(dbg_ctl, "Hook invoked: %s (cred_type=%d, preverify_ok=%d)", hook_name, static_cast<int>(cred_type), preverify);

  bool reject = false;

  if (cred_type == TS_SSL_PEER_CRED_RPK) {
    EVP_PKEY *peer_rpk = TSSslPeerVerifyCTXPeerRPKGet(verify_ctx);
    int       bits     = peer_rpk ? EVP_PKEY_bits(peer_rpk) : 0;
    Dbg(dbg_ctl, "Peer presented RPK: key_ptr=%p, bits=%d", peer_rpk, bits);

    if (opt_reject_rpk) {
      Dbg(dbg_ctl, "Rejecting RPK per configuration");
      reject = true;
    }
    if (opt_require_x509) {
      Dbg(dbg_ctl, "Rejecting RPK because X.509 is required");
      reject = true;
    }
  } else if (cred_type == TS_SSL_PEER_CRED_X509) {
    TSSslVerifyCTX store_ctx = TSSslPeerVerifyCTXStoreCTXGet(verify_ctx);
    Dbg(dbg_ctl, "Peer presented X.509: store_ctx=%p", store_ctx);

    if (opt_reject_x509) {
      Dbg(dbg_ctl, "Rejecting X.509 per configuration");
      reject = true;
    }
    if (opt_require_rpk) {
      Dbg(dbg_ctl, "Rejecting X.509 because RPK is required");
      reject = true;
    }
  } else {
    Dbg(dbg_ctl, "Peer presented NONE");
    if (opt_require_rpk || opt_require_x509) {
      reject = true;
    }
  }

  if (reject) {
    Dbg(dbg_ctl, "Failing handshake via TSVConnReenableEx(TS_EVENT_ERROR)");
    TSVConnReenableEx(ssl_vc, TS_EVENT_ERROR);
  } else {
    Dbg(dbg_ctl, "Approving handshake via TSVConnReenable");
    TSVConnReenable(ssl_vc);
  }

  return TS_SUCCESS;
}

} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--client") == 0) {
      opt_client = true;
    } else if (strcmp(argv[i], "--server") == 0) {
      opt_server = true;
    } else if (strcmp(argv[i], "--reject-rpk") == 0) {
      opt_reject_rpk = true;
    } else if (strcmp(argv[i], "--reject-x509") == 0) {
      opt_reject_x509 = true;
    } else if (strcmp(argv[i], "--require-rpk") == 0) {
      opt_require_rpk = true;
    } else if (strcmp(argv[i], "--require-x509") == 0) {
      opt_require_x509 = true;
    }
  }

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSCont cont = TSContCreate(&CB_peer_verify, TSMutexCreate());
  if (opt_client) {
    TSHttpHookAdd(TS_SSL_PEER_VERIFY_CLIENT_HOOK, cont);
    Dbg(dbg_ctl, "Registered on TS_SSL_PEER_VERIFY_CLIENT_HOOK");
  }
  if (opt_server) {
    TSHttpHookAdd(TS_SSL_PEER_VERIFY_SERVER_HOOK, cont);
    Dbg(dbg_ctl, "Registered on TS_SSL_PEER_VERIFY_SERVER_HOOK");
  }
}
