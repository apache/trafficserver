/** @file

  Peer verification example plugin.

  Demonstrates how to use TS_SSL_PEER_VERIFY_CLIENT_HOOK and
  TS_SSL_PEER_VERIFY_SERVER_HOOK to inspect and verify peer credentials,
  supporting both X.509 certificates and RFC 7250 raw public keys (RPK).

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
#include <cinttypes>
#include <ts/ts.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#define PLUGIN_NAME "peer_verify"
#define PCP         "[" PLUGIN_NAME "] "

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

static void
debug_certificate(const char *msg, const X509_NAME *name)
{
  BIO *bio;

  if (name == nullptr) {
    return;
  }

  bio = BIO_new(BIO_s_mem());
  if (bio == nullptr) {
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    long  len;
    char *ptr;
    len = BIO_get_mem_data(bio, &ptr);
    Dbg(dbg_ctl, "%s %.*s", msg, static_cast<int>(len), ptr);
  }

  BIO_free(bio);
}

int
CB_peer_verify(TSCont /* contp */, TSEvent event, void *edata)
{
  TSSslPeerVerifyCTX verify_ctx = static_cast<TSSslPeerVerifyCTX>(edata);
  TSVConn            ssl_vc     = TSSslPeerVerifyCTXVConnGet(verify_ctx);
  TSSslPeerCredType  cred_type  = TSSslPeerVerifyCTXCredTypeGet(verify_ctx);
  int                preverify  = TSSslPeerVerifyCTXPreverifyGet(verify_ctx);
  const char        *direction  = (event == TS_EVENT_SSL_PEER_VERIFY_CLIENT) ? "client" : "server (origin)";

  Dbg(dbg_ctl, "Verifying peer %s credentials (preverify_ok=%d)", direction, preverify);

  switch (cred_type) {
  case TS_SSL_PEER_CRED_X509: {
    TSSslVerifyCTX store_ctx = TSSslPeerVerifyCTXStoreCTXGet(verify_ctx);
    Dbg(dbg_ctl, "Peer presented X.509 certificate (store_ctx=%p)", store_ctx);
    TSSslConnection sslobj = TSVConnSslConnectionGet(ssl_vc);
    SSL            *ssl    = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_AT_LEAST_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      debug_certificate("Peer certificate subject CN is", X509_get_subject_name(cert));
      debug_certificate("Peer certificate issuer CN is", X509_get_issuer_name(cert));
      X509_free(cert);
    }
    break;
  }
  case TS_SSL_PEER_CRED_RPK: {
    EVP_PKEY *peer_rpk = TSSslPeerVerifyCTXPeerRPKGet(verify_ctx);
    Dbg(dbg_ctl, "Peer presented RFC 7250 Raw Public Key (peer_rpk=%p, bits=%d, type=%d)", peer_rpk,
        peer_rpk != nullptr ? EVP_PKEY_bits(peer_rpk) : 0, peer_rpk != nullptr ? EVP_PKEY_base_id(peer_rpk) : 0);
    break;
  }
  case TS_SSL_PEER_CRED_NONE:
  default:
    Dbg(dbg_ctl, "Peer presented no credentials");
    break;
  }

  // Allow the connection to proceed
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  TSCont                   cb_peer = nullptr;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
  } else if (nullptr == (cb_peer = TSContCreate(&CB_peer_verify, TSMutexCreate()))) {
    TSError(PCP "Failed to create peer verify callback continuation");
  } else {
    TSHttpHookAdd(TS_SSL_PEER_VERIFY_CLIENT_HOOK, cb_peer);
    TSHttpHookAdd(TS_SSL_PEER_VERIFY_SERVER_HOOK, cb_peer);
  }
}
