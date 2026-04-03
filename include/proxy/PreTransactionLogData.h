/** @file

  PreTransactionLogData populates LogData for requests that fail before
  HttpSM creation.

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

#include "proxy/logging/TransactionLogData.h"

#include <string>

/** Populate LogData for requests that never created an HttpSM.
 *
 * Malformed HTTP/2 or HTTP/3 request headers can be rejected while the
 * connection is still decoding and validating the stream, before the request
 * progresses far enough to create an HttpSM.  This class carries the
 * copied request and session metadata needed to emit a best-effort
 * transaction log entry for those failures.
 *
 * Unlike TransactionLogData (which reads from a live HttpSM), this class
 * owns its milestones, addresses, and strings because the originating
 * stream is about to be destroyed.
 */
class PreTransactionLogData : public TransactionLogData
{
public:
  PreTransactionLogData() = default;

  ~PreTransactionLogData() override
  {
    if (owned_client_request.valid()) {
      owned_client_request.destroy();
    }
  }

  // ===== Milestones =====

  TransactionMilestones const *
  get_milestones() const override
  {
    return &owned_milestones;
  }

  // ===== Headers =====

  HTTPHdr *
  get_client_request() const override
  {
    if (owned_client_request.valid()) {
      return const_cast<HTTPHdr *>(&owned_client_request);
    }
    return nullptr;
  }

  // ===== Client request URL / path =====

  const char *
  get_client_req_url_str() const override
  {
    return owned_url.empty() ? nullptr : owned_url.c_str();
  }
  int
  get_client_req_url_len() const override
  {
    return static_cast<int>(owned_url.size());
  }
  const char *
  get_client_req_url_path_str() const override
  {
    return owned_path.empty() ? nullptr : owned_path.c_str();
  }
  int
  get_client_req_url_path_len() const override
  {
    return static_cast<int>(owned_path.size());
  }

  // ===== Client addressing =====

  sockaddr const *
  get_client_addr() const override
  {
    return &owned_client_addr.sa;
  }
  sockaddr const *
  get_client_src_addr() const override
  {
    return &owned_client_src_addr.sa;
  }
  sockaddr const *
  get_client_dst_addr() const override
  {
    return &owned_client_dst_addr.sa;
  }
  uint16_t
  get_client_port() const override
  {
    return m_client_port;
  }

  // ===== Squid codes =====

  SquidLogCode
  get_log_code() const override
  {
    return m_log_code;
  }
  SquidHitMissCode
  get_hit_miss_code() const override
  {
    return m_hit_miss_code;
  }
  SquidHierarchyCode
  get_hier_code() const override
  {
    return m_hier_code;
  }

  // ===== Transaction identifiers =====

  int64_t
  get_connection_id() const override
  {
    return m_connection_id;
  }
  int
  get_transaction_id() const override
  {
    return m_transaction_id;
  }

  // ===== Protocol info =====

  const char *
  get_client_protocol() const override
  {
    return owned_client_protocol_str.empty() ? nullptr : owned_client_protocol_str.c_str();
  }

  // ===== Connection flags =====

  bool
  get_client_connection_is_ssl() const override
  {
    return m_client_connection_is_ssl;
  }

  // ===== Server transaction count =====

  int64_t
  get_server_transact_count() const override
  {
    return m_server_transact_count;
  }

  // ===== Fallback fields for pre-transaction logging =====

  std::string_view
  get_method() const override
  {
    return owned_method;
  }
  std::string_view
  get_scheme() const override
  {
    return owned_scheme;
  }
  std::string_view
  get_client_protocol_str() const override
  {
    return owned_client_protocol_str;
  }

  // ===== Owned backing storage (public for ProxyTransaction to populate). =====

  HTTPHdr               owned_client_request;
  TransactionMilestones owned_milestones;
  IpEndpoint            owned_client_addr     = {};
  IpEndpoint            owned_client_src_addr = {};
  IpEndpoint            owned_client_dst_addr = {};

  std::string owned_method;
  std::string owned_scheme;
  std::string owned_authority;
  std::string owned_path;
  std::string owned_url;
  std::string owned_client_protocol_str;

  // ===== Simple fields (public for ProxyTransaction to set). =====

  uint16_t           m_client_port              = 0;
  SquidLogCode       m_log_code                 = SquidLogCode::EMPTY;
  SquidHitMissCode   m_hit_miss_code            = SQUID_MISS_NONE;
  SquidHierarchyCode m_hier_code                = SquidHierarchyCode::NONE;
  int64_t            m_connection_id            = 0;
  int                m_transaction_id           = 0;
  bool               m_client_connection_is_ssl = false;
  int64_t            m_server_transact_count    = 0;
};
