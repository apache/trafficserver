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

#include "iocore/net/quic/QUICGlobals.h"

#include <cstring>
#include <fstream>

#include "iocore/net/QUICMultiCertConfigLoader.h"

#include "iocore/net/quic/QUICStats.h"
#include "iocore/net/quic/QUICConfig.h"
#include "iocore/net/quic/QUICConnection.h"

#include "iocore/net/quic/QUICTLS.h"
#include <openssl/ssl.h>

#define QUICGlobalDebug(fmt, ...) Debug("quic_global", fmt, ##__VA_ARGS__)

QuicStatsBlock quic_rsb;

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
QUIC::ssl_client_new_session(SSL *ssl, SSL_SESSION *session)
{
#if TS_HAS_QUICHE
#else
  QUICTLS *qtls            = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  const char *session_file = qtls->session_file();
  auto file                = BIO_new_file(session_file, "w");

  if (file == nullptr) {
    QUICGlobalDebug("Could not write TLS session in %s", session_file);
    return 0;
  }

  PEM_write_bio_SSL_SESSION(file, session);
  BIO_free(file);
#endif
  return 0;
}

void
QUIC::_register_stats()
{
  ts::Metrics::Counter &metrics = ts::Metrics::Counter::getInstance();
  // Transferred packet counts
  quic_rsb.total_packets_sent = metrics.createPtr("proxy.process.quic.total_packets_sent");

  // quic_rsb.total_packets_retransmitted = metrics.createPtr("proxy.process.quic.total_packets_retransmitted");
  // quic_rsb.total_packets_received      = metrics.createPtr("proxy.process.quic.total_packets_received");
}
