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

/****************************************************************************

  Net Subsystem


**************************************************************************/
#pragma once

#include "api/Metrics.h"

using ts::Metrics;

// Net Stats

struct NetStatsBlock {
  Metrics::IntType *accepts_currently_open;
  Metrics::IntType *calls_to_read_nodata;
  Metrics::IntType *calls_to_read;
  Metrics::IntType *calls_to_readfromnet;
  Metrics::IntType *calls_to_write_nodata;
  Metrics::IntType *calls_to_write;
  Metrics::IntType *calls_to_writetonet;
  Metrics::IntType *connections_currently_open;
  Metrics::IntType *connections_throttled_in;
  Metrics::IntType *connections_throttled_out;
  Metrics::IntType *default_inactivity_timeout_applied;
  Metrics::IntType *default_inactivity_timeout_count;
  Metrics::IntType *fastopen_attempts;
  Metrics::IntType *fastopen_successes;
  Metrics::IntType *handler_run;
  Metrics::IntType *handler_run_count;
  Metrics::IntType *inactivity_cop_lock_acquire_failure;
  Metrics::IntType *keep_alive_queue_timeout_count;
  Metrics::IntType *keep_alive_queue_timeout_total;
  Metrics::IntType *read_bytes;
  Metrics::IntType *read_bytes_count;
  Metrics::IntType *requests_max_throttled_in;
  Metrics::IntType *tunnel_total_client_connections_blind_tcp;
  Metrics::IntType *tunnel_current_client_connections_blind_tcp;
  Metrics::IntType *tunnel_total_server_connections_blind_tcp;
  Metrics::IntType *tunnel_current_server_connections_blind_tcp;
  Metrics::IntType *tunnel_total_client_connections_tls_tunnel;
  Metrics::IntType *tunnel_current_client_connections_tls_tunnel;
  Metrics::IntType *tunnel_total_server_connections_tls;
  Metrics::IntType *tunnel_current_server_connections_tls;
  Metrics::IntType *tunnel_total_client_connections_tls_forward;
  Metrics::IntType *tunnel_current_client_connections_tls_forward;
  Metrics::IntType *tunnel_total_client_connections_tls_partial_blind;
  Metrics::IntType *tunnel_current_client_connections_tls_partial_blind;
  Metrics::IntType *tunnel_total_client_connections_tls_http;
  Metrics::IntType *tunnel_current_client_connections_tls_http;
  Metrics::IntType *socks_connections_currently_open;
  Metrics::IntType *socks_connections_successful;
  Metrics::IntType *socks_connections_unsuccessful;
  Metrics::IntType *tcp_accept;
  Metrics::IntType *write_bytes;
  Metrics::IntType *write_bytes_count;
};

extern NetStatsBlock net_rsb;

#define SSL_HANDSHAKE_WANT_READ    6
#define SSL_HANDSHAKE_WANT_WRITE   7
#define SSL_HANDSHAKE_WANT_ACCEPT  8
#define SSL_HANDSHAKE_WANT_CONNECT 9

#include "tscore/ink_platform.h"
#include "P_EventSystem.h"
#include "I_Net.h"
#include "P_NetVConnection.h"
#include "P_UnixNet.h"
#include "P_UnixNetProcessor.h"
#include "P_NetAccept.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixPollDescriptor.h"
#include "P_Socks.h"
#include "P_CompletionUtil.h"
#include "P_NetVCTest.h"

static constexpr ts::ModuleVersion NET_SYSTEM_MODULE_INTERNAL_VERSION(NET_SYSTEM_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE);

// For very verbose iocore debugging.
#ifndef DEBUG
#define NetDbg(dbg_ctl, fmt, ...)
#else
#define NetDbg(dbg_ctl, fmt, ...) Dbg(dbg_ctl, fmt, ##__VA_ARGS__)
#endif

/// Default amount of buffer space to use for the initial read on an incoming connection.
/// This is an IOBufferBlock index, not the size in bytes.
static size_t const CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX = BUFFER_SIZE_INDEX_4K;
