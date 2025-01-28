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

#include "tsutil/Metrics.h"

// Net Stats
using ts::Metrics;

struct NetStatsBlock {
  Metrics::Gauge::AtomicType   *accepts_currently_open;
  Metrics::Counter::AtomicType *calls_to_read_nodata;
  Metrics::Counter::AtomicType *calls_to_read;
  Metrics::Counter::AtomicType *calls_to_readfromnet;
  Metrics::Counter::AtomicType *calls_to_write_nodata;
  Metrics::Counter::AtomicType *calls_to_write;
  Metrics::Counter::AtomicType *calls_to_writetonet;
  Metrics::Gauge::AtomicType   *connections_currently_open;
  Metrics::Counter::AtomicType *connections_throttled_in;
  Metrics::Counter::AtomicType *per_client_connections_throttled_in;
  Metrics::Counter::AtomicType *connections_throttled_out;
  Metrics::Counter::AtomicType *default_inactivity_timeout_applied;
  Metrics::Counter::AtomicType *default_inactivity_timeout_count;
  Metrics::Counter::AtomicType *fastopen_attempts;
  Metrics::Counter::AtomicType *fastopen_successes;
  Metrics::Counter::AtomicType *handler_run;
  Metrics::Counter::AtomicType *handler_run_count;
  Metrics::Counter::AtomicType *inactivity_cop_lock_acquire_failure;
  Metrics::Counter::AtomicType *keep_alive_queue_timeout_count;
  Metrics::Counter::AtomicType *keep_alive_queue_timeout_total;
  Metrics::Counter::AtomicType *read_bytes;
  Metrics::Counter::AtomicType *read_bytes_count;
  Metrics::Counter::AtomicType *requests_max_throttled_in;
  Metrics::Counter::AtomicType *tunnel_total_client_connections_blind_tcp;
  Metrics::Gauge::AtomicType   *tunnel_current_client_connections_blind_tcp;
  Metrics::Counter::AtomicType *tunnel_total_server_connections_blind_tcp;
  Metrics::Gauge::AtomicType   *tunnel_current_server_connections_blind_tcp;
  Metrics::Counter::AtomicType *tunnel_total_client_connections_tls_tunnel;
  Metrics::Gauge::AtomicType   *tunnel_current_client_connections_tls_tunnel;
  Metrics::Counter::AtomicType *tunnel_total_server_connections_tls;
  Metrics::Gauge::AtomicType   *tunnel_current_server_connections_tls;
  Metrics::Counter::AtomicType *tunnel_total_client_connections_tls_forward;
  Metrics::Gauge::AtomicType   *tunnel_current_client_connections_tls_forward;
  Metrics::Counter::AtomicType *tunnel_total_client_connections_tls_partial_blind;
  Metrics::Gauge::AtomicType   *tunnel_current_client_connections_tls_partial_blind;
  Metrics::Counter::AtomicType *tunnel_total_client_connections_tls_http;
  Metrics::Gauge::AtomicType   *tunnel_current_client_connections_tls_http;
  Metrics::Gauge::AtomicType   *socks_connections_currently_open;
  Metrics::Counter::AtomicType *socks_connections_successful;
  Metrics::Counter::AtomicType *socks_connections_unsuccessful;
  Metrics::Counter::AtomicType *tcp_accept;
  Metrics::Counter::AtomicType *write_bytes;
  Metrics::Counter::AtomicType *write_bytes_count;
  Metrics::Gauge::AtomicType   *connection_tracker_table_size;
};

extern NetStatsBlock net_rsb;

#define SSL_HANDSHAKE_WANT_READ    6
#define SSL_HANDSHAKE_WANT_WRITE   7
#define SSL_HANDSHAKE_WANT_ACCEPT  8
#define SSL_HANDSHAKE_WANT_CONNECT 9

#include "iocore/net/Net.h"

static constexpr ts::ModuleVersion NET_SYSTEM_MODULE_INTERNAL_VERSION(NET_SYSTEM_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE);

#define NetDbg(dbg_ctl, fmt, ...) Dbg(dbg_ctl, fmt, ##__VA_ARGS__)
