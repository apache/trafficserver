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

/************************************************************************

   Net.cc


************************************************************************/

#include "iocore/net/SSLAPIHooks.h"
#include "P_Net.h"
#include <utility>

NetStatsBlock net_rsb;

// All in milli-seconds
int net_event_period   = 10;
int net_accept_period  = 10;
int net_retry_delay    = 10;
int net_throttle_delay = 50; /* milliseconds */

// For the in/out congestion control: ToDo: this probably would be better as ports: specifications
std::string_view net_ccp_in;
std::string_view net_ccp_out;

static inline void
configure_net()
{
  REC_RegisterConfigUpdateFunc("proxy.config.net.connections_throttle", change_net_connections_throttle, nullptr);
  REC_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");

  REC_EstablishStaticConfigInt32(net_retry_delay, "proxy.config.net.retry_delay");
  REC_EstablishStaticConfigInt32(net_throttle_delay, "proxy.config.net.throttle_delay");

  // These are not reloadable
  REC_ReadConfigInteger(net_event_period, "proxy.config.net.event_period");
  REC_ReadConfigInteger(net_accept_period, "proxy.config.net.accept_period");

  // This is kinda fugly, but better than it was before (on every connection in and out)
  // Note that these would need to be ats_free()'d if we ever want to clean that up, but
  // we have no good way of dealing with that on such globals I think?
  RecString ccp;

  REC_ReadConfigStringAlloc(ccp, "proxy.config.net.tcp_congestion_control_in");
  if (ccp && *ccp != '\0') {
    net_ccp_in = ccp;
  } else {
    ats_free(ccp);
  }

  REC_ReadConfigStringAlloc(ccp, "proxy.config.net.tcp_congestion_control_out");
  if (ccp && *ccp != '\0') {
    net_ccp_out = ccp;
  } else {
    ats_free(ccp);
  }
}

static inline void
register_net_stats()
{
  net_rsb.accepts_currently_open                    = Counter::CreatePtr("proxy.process.net.accepts_currently_open");
  net_rsb.calls_to_read                             = Counter::CreatePtr("proxy.process.net.calls_to_read");
  net_rsb.calls_to_read_nodata                      = Counter::CreatePtr("proxy.process.net.calls_to_read_nodata");
  net_rsb.calls_to_readfromnet                      = Counter::CreatePtr("proxy.process.net.calls_to_readfromnet");
  net_rsb.calls_to_write                            = Counter::CreatePtr("proxy.process.net.calls_to_write");
  net_rsb.calls_to_write_nodata                     = Counter::CreatePtr("proxy.process.net.calls_to_write_nodata");
  net_rsb.calls_to_writetonet                       = Counter::CreatePtr("proxy.process.net.calls_to_writetonet");
  net_rsb.connections_currently_open                = Counter::CreatePtr("proxy.process.net.connections_currently_open");
  net_rsb.connections_throttled_in                  = Counter::CreatePtr("proxy.process.net.connections_throttled_in");
  net_rsb.connections_throttled_out                 = Counter::CreatePtr("proxy.process.net.connections_throttled_out");
  net_rsb.tunnel_total_client_connections_blind_tcp = Counter::CreatePtr("proxy.process.tunnel.total_client_connections_blind_tcp");
  net_rsb.tunnel_current_client_connections_blind_tcp =
    Counter::CreatePtr("proxy.process.tunnel.current_client_connections_blind_tcp");
  net_rsb.tunnel_total_server_connections_blind_tcp = Counter::CreatePtr("proxy.process.tunnel.total_server_connections_blind_tcp");
  net_rsb.tunnel_current_server_connections_blind_tcp =
    Counter::CreatePtr("proxy.process.tunnel.current_server_connections_blind_tcp");
  net_rsb.tunnel_total_client_connections_tls_tunnel =
    Counter::CreatePtr("proxy.process.tunnel.total_client_connections_tls_tunnel");
  net_rsb.tunnel_current_client_connections_tls_tunnel =
    Counter::CreatePtr("proxy.process.tunnel.current_client_connections_tls_tunnel");
  net_rsb.tunnel_total_client_connections_tls_forward =
    Counter::CreatePtr("proxy.process.tunnel.total_client_connections_tls_forward");
  net_rsb.tunnel_current_client_connections_tls_forward =
    Counter::CreatePtr("proxy.process.tunnel.current_client_connections_tls_forward");
  net_rsb.tunnel_total_client_connections_tls_partial_blind =
    Counter::CreatePtr("proxy.process.tunnel.total_client_connections_tls_partial_blind");
  net_rsb.tunnel_current_client_connections_tls_partial_blind =
    Counter::CreatePtr("proxy.process.tunnel.current_client_connections_tls_partial_blind");
  net_rsb.tunnel_total_client_connections_tls_http = Counter::CreatePtr("proxy.process.tunnel.total_client_connections_tls_http");
  net_rsb.tunnel_current_client_connections_tls_http =
    Counter::CreatePtr("proxy.process.tunnel.current_client_connections_tls_http");
  net_rsb.tunnel_total_server_connections_tls   = Counter::CreatePtr("proxy.process.tunnel.total_server_connections_tls");
  net_rsb.tunnel_current_server_connections_tls = Counter::CreatePtr("proxy.process.tunnel.current_server_connections_tls");
  net_rsb.default_inactivity_timeout_applied    = Counter::CreatePtr("proxy.process.net.default_inactivity_timeout_applied");
  net_rsb.default_inactivity_timeout_count      = Counter::CreatePtr("proxy.process.net.default_inactivity_timeout_count");
  net_rsb.fastopen_attempts                     = Counter::CreatePtr("proxy.process.net.fastopen_out.attempts");
  net_rsb.fastopen_successes                    = Counter::CreatePtr("proxy.process.net.fastopen_out.successes");
  net_rsb.handler_run                           = Counter::CreatePtr("proxy.process.net.net_handler_run");
  net_rsb.inactivity_cop_lock_acquire_failure   = Counter::CreatePtr("proxy.process.net.inactivity_cop_lock_acquire_failure");
  net_rsb.keep_alive_queue_timeout_count        = Counter::CreatePtr("proxy.process.net.dynamic_keep_alive_timeout_in_count");
  net_rsb.keep_alive_queue_timeout_total        = Counter::CreatePtr("proxy.process.net.dynamic_keep_alive_timeout_in_total");
  net_rsb.read_bytes                            = Counter::CreatePtr("proxy.process.net.read_bytes");
  net_rsb.read_bytes_count                      = Counter::CreatePtr("proxy.process.net.read_bytes_count");
  net_rsb.requests_max_throttled_in             = Counter::CreatePtr("proxy.process.net.max.requests_throttled_in");
  net_rsb.socks_connections_currently_open      = Counter::CreatePtr("proxy.process.socks.connections_currently_open");
  net_rsb.socks_connections_successful          = Counter::CreatePtr("proxy.process.socks.connections_successful");
  net_rsb.socks_connections_unsuccessful        = Counter::CreatePtr("proxy.process.socks.connections_unsuccessful");
  net_rsb.tcp_accept                            = Counter::CreatePtr("proxy.process.tcp.total_accepts");
  net_rsb.write_bytes                           = Counter::CreatePtr("proxy.process.net.write_bytes");
  net_rsb.write_bytes_count                     = Counter::CreatePtr("proxy.process.net.write_bytes_count");
}

void
ink_net_init(ts::ModuleVersion version)
{
  static int init_called = 0;

  ink_release_assert(version.check(NET_SYSTEM_MODULE_INTERNAL_VERSION));

  if (!init_called) {
    configure_net();
    register_net_stats();
    init_global_ssl_hooks();
  }

  init_called = 1;
}
