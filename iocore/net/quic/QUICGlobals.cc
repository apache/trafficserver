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

#include "QUICGlobals.h"

#include <cstring>

#include "P_SSLNextProtocolSet.h"

#include "QUICStats.h"
#include "QUICConfig.h"
#include "QUICConnection.h"

#define QUICGlobalDebug(fmt, ...) Debug("quic_global", fmt, ##__VA_ARGS__)
#define QUICGlobalQCDebug(qc, fmt, ...) Debug("quic_global", "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

RecRawStatBlock *quic_rsb;

int QUIC::ssl_quic_qc_index  = -1;
int QUIC::ssl_quic_tls_index = -1;

void
QUIC::init()
{
  QUIC::_register_stats();
  ssl_quic_qc_index  = SSL_get_ex_new_index(0, (void *)"QUICConnection index", nullptr, nullptr, nullptr);
  ssl_quic_tls_index = SSL_get_ex_new_index(0, (void *)"QUICTLS index", nullptr, nullptr, nullptr);
}

int
QUIC::ssl_select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned inlen,
                               void *)
{
  const unsigned char *npn;
  unsigned npnsz     = 0;
  QUICConnection *qc = static_cast<QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));

  qc->next_protocol_set()->advertiseProtocols(&npn, &npnsz);
  if (SSL_select_next_proto((unsigned char **)out, outlen, npn, npnsz, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_OK;
  }

  *out    = nullptr;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}

int
QUIC::ssl_client_new_session(SSL *ssl, SSL_SESSION *session)
{
  QUICConfig::scoped_config params;
  const char *session_file = params->session_file();
  auto file                = BIO_new_file(session_file, "w");

  if (file == nullptr) {
    QUICGlobalDebug("Could not write TLS session in %s", session_file);
    return 0;
  }

  PEM_write_bio_SSL_SESSION(file, session);
  BIO_free(file);
  return 0;
}

int
QUIC::ssl_cert_cb(SSL *ssl, void * /*arg*/)
{
  SSL_CTX *ctx       = nullptr;
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
    cc = lookup->find((char *)servername);
    if (cc && cc->ctx) {
      ctx = cc->ctx;
    }
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == nullptr) {
    QUICFiveTuple five_tuple = qc->five_tuple();
    IpEndpoint ip            = five_tuple.destination();
    cc                       = lookup->find(ip);

    if (cc && cc->ctx) {
      ctx = cc->ctx;
    }
  }

  bool found = true;
  if (ctx != nullptr) {
    SSL_set_SSL_CTX(ssl, ctx);
  } else {
    found = false;
  }

  ctx = SSL_get_SSL_CTX(ssl);

  QUICGlobalQCDebug(qc, "%s SSL_CTX %p for requested name '%s'", found ? "found" : "using", ctx, servername);

  return 1;
}

int
QUIC::ssl_sni_cb(SSL *ssl, int * /*ad*/, void * /*arg*/)
{
  // XXX: add SNIConfig support ?
  // XXX: add TRANSPORT_BLIND_TUNNEL support ?
  return 1;
}

void
QUIC::_register_stats()
{
  quic_rsb = RecAllocateRawStatBlock(static_cast<int>(QUICStats::count));

  // Transfered packet counts
  RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_sent", RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(QUICStats::total_packets_sent_stat), RecRawStatSyncSum);
  // RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_retransmitted", RECD_INT, RECP_PERSISTENT,
  //                              static_cast<int>(quic_total_packets_retransmitted_stat), RecRawStatSyncSum);
  // RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_received", RECD_INT, RECP_PERSISTENT,
  //                            static_cast<int>(quic_total_packets_received_stat), RecRawStatSyncSum);
}
