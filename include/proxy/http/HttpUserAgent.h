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

#pragma once

#include "proxy/http/HttpVCTable.h"
#include "proxy/Milestones.h"

#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/net/TLSALPNSupport.h"
#include "proxy/ProxyTransaction.h"
#include "records/RecHttp.h"
#include "iocore/net/TLSBasicSupport.h"
#include "iocore/net/TLSSessionResumptionSupport.h"

struct ClientTransactionInfo {
  int id{-1};
  int priority_weight{-1};
  int priority_dependence{-1};
};

struct ClientConnectionInfo {
  bool tcp_reused{false};
  bool ssl_reused{false};
  bool connection_is_ssl{false};

  char const *protocol{"-"};
  char const *sec_protocol{"-"};
  char const *cipher_suite{"-"};
  char const *curve{"-"};

  int alpn_id{SessionProtocolNameRegistry::INVALID};
};

class HttpUserAgent
{
public:
  HttpVCTableEntry *get_entry() const;
  void              set_entry(HttpVCTableEntry *entry);

  IOBufferReader *get_raw_buffer_reader();
  void            set_raw_buffer_reader(IOBufferReader *raw_buffer_reader);

  ProxyTransaction *get_txn() const;
  void              set_txn(ProxyTransaction *txn, TransactionMilestones &milestones);

  int get_client_connection_id() const;

  int get_client_transaction_id() const;

  int get_client_transaction_priority_weight() const;

  int get_client_transaction_priority_dependence() const;

  bool get_client_tcp_reused() const;

  bool get_client_ssl_reused() const;

  bool get_client_connection_is_ssl() const;

  char const *get_client_protocol() const;

  char const *get_client_sec_protocol() const;

  char const *get_client_cipher_suite() const;

  char const *get_client_curve() const;

  int get_client_alpn_id() const;

private:
  HttpVCTableEntry *m_entry{nullptr};
  IOBufferReader   *m_raw_buffer_reader{nullptr};
  ProxyTransaction *m_txn{nullptr};

  ClientConnectionInfo m_conn_info{};

  int                   m_client_connection_id{-1};
  ClientTransactionInfo m_txn_info{};

  void save_transaction_info();
};

inline HttpVCTableEntry *
HttpUserAgent::get_entry() const
{
  return m_entry;
}

inline void
HttpUserAgent::set_entry(HttpVCTableEntry *entry)
{
  m_entry = entry;
}

inline IOBufferReader *
HttpUserAgent::get_raw_buffer_reader()
{
  return m_raw_buffer_reader;
}

inline void
HttpUserAgent::set_raw_buffer_reader(IOBufferReader *raw_buffer_reader)
{
  m_raw_buffer_reader = raw_buffer_reader;
}

inline ProxyTransaction *
HttpUserAgent::get_txn() const
{
  return m_txn;
}

inline void
HttpUserAgent::set_txn(ProxyTransaction *txn, TransactionMilestones &milestones)
{
  m_txn = txn;

  // It seems to be possible that the m_txn pointer will go stale before log
  // entries for this HTTP transaction are generated. Therefore, collect
  // information that may be needed for logging.
  this->save_transaction_info();
  if (auto p{txn->get_proxy_ssn()}; p) {
    m_client_connection_id = p->connection_id();
  }

  m_conn_info.tcp_reused = !txn->is_first_transaction();

  auto netvc{txn->get_netvc()};

  if (auto tbs = netvc->get_service<TLSBasicSupport>()) {
    m_conn_info.connection_is_ssl = true;
    if (auto sec_protocol{tbs->get_tls_protocol_name()}; sec_protocol) {
      m_conn_info.sec_protocol = sec_protocol;
    } else {
      m_conn_info.sec_protocol = "-";
    }
    if (auto cipher{tbs->get_tls_cipher_suite()}; cipher) {
      m_conn_info.cipher_suite = cipher;
    } else {
      m_conn_info.cipher_suite = "-";
    }
    if (auto curve{tbs->get_tls_curve()}; curve) {
      m_conn_info.curve = curve;
    } else {
      m_conn_info.curve = "-";
    }

    if (!m_conn_info.tcp_reused) {
      // Copy along the TLS handshake timings
      milestones[TS_MILESTONE_TLS_HANDSHAKE_START] = tbs->get_tls_handshake_begin_time();
      milestones[TS_MILESTONE_TLS_HANDSHAKE_END]   = tbs->get_tls_handshake_end_time();
    }
  }

  if (auto as = netvc->get_service<ALPNSupport>()) {
    m_conn_info.alpn_id = as->get_negotiated_protocol_id();
  }

  if (auto tsrs = netvc->get_service<TLSSessionResumptionSupport>()) {
    m_conn_info.ssl_reused = tsrs->getSSLSessionCacheHit();
  }

  if (auto protocol_str{txn->get_protocol_string()}; protocol_str) {
    m_conn_info.protocol = protocol_str;
  } else {
    m_conn_info.protocol = "-";
  }
}

inline int
HttpUserAgent::get_client_connection_id() const
{
  return m_client_connection_id;
}

inline int
HttpUserAgent::get_client_transaction_id() const
{
  return m_txn_info.id;
}

inline int
HttpUserAgent::get_client_transaction_priority_weight() const
{
  return m_txn_info.priority_weight;
}

inline int
HttpUserAgent::get_client_transaction_priority_dependence() const
{
  return m_txn_info.priority_dependence;
}

inline bool
HttpUserAgent::get_client_tcp_reused() const
{
  return m_conn_info.tcp_reused;
}

inline bool
HttpUserAgent::get_client_ssl_reused() const
{
  return m_conn_info.ssl_reused;
}

inline bool
HttpUserAgent::get_client_connection_is_ssl() const
{
  return m_conn_info.connection_is_ssl;
}

inline char const *
HttpUserAgent::get_client_protocol() const
{
  return m_conn_info.protocol;
}

inline char const *
HttpUserAgent::get_client_sec_protocol() const
{
  return m_conn_info.sec_protocol;
}

inline char const *
HttpUserAgent::get_client_cipher_suite() const
{
  return m_conn_info.cipher_suite;
}

inline char const *
HttpUserAgent::get_client_curve() const
{
  return m_conn_info.curve;
}

inline int
HttpUserAgent::get_client_alpn_id() const
{
  return m_conn_info.alpn_id;
}

inline void
HttpUserAgent::save_transaction_info()
{
  m_txn_info = {m_txn->get_transaction_id(), m_txn->get_transaction_priority_weight(),
                m_txn->get_transaction_priority_dependence()};
}
