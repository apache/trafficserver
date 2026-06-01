/** @file

  NonHttpSmLogData populates LogData for access-log entries that cannot be
  backed by an HttpSM.

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

#include "proxy/Milestones.h"
#include "proxy/hdrs/HTTP.h"
#include "tscore/ink_inet.h"

#include <string>

/** Owns access-log data for entries that cannot be backed by an @c HttpSM.
 *
 * Normal transaction access logging is expected to use data extracted from a
 * live @c HttpSM. This type is for exceptional client-facing failures that need
 * transaction-log visibility but occur before an @c HttpSM exists, such as
 * malformed HTTP/2 or HTTP/3 request headers rejected during protocol
 * validation. It may also be used for connection-level failures, such as TLS
 * handshake errors, when operators need those events in the access log.
 *
 * Because the protocol stream or connection state may be destroyed immediately
 * after the failure is handled, this object owns the copied headers,
 * addresses, milestones, protocol strings, and outcome fields needed by
 * @c LogAccess. Fields that require an @c HttpSM, origin transaction, cache
 * lookup, or server response are intentionally left unset and marshal through
 * the normal default values.
 *
 * This path should remain narrow. If an @c HttpSM exists, prefer the standard
 * @c HttpSM-backed logging path so normal transactions do not pay for extra
 * copying or exceptional state.
 */
class NonHttpSmLogData
{
public:
  NonHttpSmLogData() = default;

  ~NonHttpSmLogData()
  {
    if (owned_client_request.valid()) {
      owned_client_request.destroy();
    }
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
