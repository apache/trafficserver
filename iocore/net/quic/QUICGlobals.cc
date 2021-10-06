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
#include <fstream>

#include "QUICMultiCertConfigLoader.h"

#include "QUICStats.h"
#include "QUICConfig.h"
#include "QUICConnection.h"

#include "QUICTLS.h"
#include <openssl/ssl.h>

#define QUICGlobalDebug(fmt, ...) Debug("quic_global", fmt, ##__VA_ARGS__)

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
QUIC::ssl_client_new_session(SSL *ssl, SSL_SESSION *session)
{
  QUICTLS *qtls            = static_cast<QUICTLS *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_tls_index));
  const char *session_file = qtls->session_file();
  auto file                = BIO_new_file(session_file, "w");

  if (file == nullptr) {
    QUICGlobalDebug("Could not write TLS session in %s", session_file);
    return 0;
  }

  PEM_write_bio_SSL_SESSION(file, session);
  BIO_free(file);
  return 0;
}

void
QUIC::_register_stats()
{
  quic_rsb = RecAllocateRawStatBlock(static_cast<int>(QUICStats::count));

  // Transferred packet counts
  RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_sent", RECD_INT, RECP_PERSISTENT,
                     static_cast<int>(QUICStats::total_packets_sent_stat), RecRawStatSyncSum);
  // RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_retransmitted", RECD_INT, RECP_PERSISTENT,
  //                              static_cast<int>(quic_total_packets_retransmitted_stat), RecRawStatSyncSum);
  // RecRegisterRawStat(quic_rsb, RECT_PROCESS, "proxy.process.quic.total_packets_received", RECD_INT, RECP_PERSISTENT,
  //                            static_cast<int>(quic_total_packets_received_stat), RecRawStatSyncSum);
}
