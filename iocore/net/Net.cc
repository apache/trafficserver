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

#include "P_Net.h"
#include <utility>

RecRawStatBlock *net_rsb = nullptr;

// All in milli-seconds
int net_config_poll_timeout = -1; // This will get set via either command line or records.config.
int net_event_period        = 10;
int net_accept_period       = 10;
int net_retry_delay         = 10;
int net_throttle_delay      = 50; /* milliseconds */

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
  }
  REC_ReadConfigStringAlloc(ccp, "proxy.config.net.tcp_congestion_control_out");
  if (ccp && *ccp != '\0') {
    net_ccp_out = ccp;
  }
}

static inline void
register_net_stats()
{
  const std::pair<const char *, Net_Stats> persistent[] = {
    {"proxy.process.net.calls_to_read", net_calls_to_read_stat},
    {"proxy.process.net.calls_to_read_nodata", net_calls_to_read_nodata_stat},
    {"proxy.process.net.calls_to_readfromnet", net_calls_to_readfromnet_stat},
    {"proxy.process.net.calls_to_readfromnet_afterpoll", net_calls_to_readfromnet_afterpoll_stat},
    {"proxy.process.net.calls_to_write", net_calls_to_write_stat},
    {"proxy.process.net.calls_to_write_nodata", net_calls_to_write_nodata_stat},
    {"proxy.process.net.calls_to_writetonet", net_calls_to_writetonet_stat},
    {"proxy.process.net.calls_to_writetonet_afterpoll", net_calls_to_writetonet_afterpoll_stat},
    {"proxy.process.net.inactivity_cop_lock_acquire_failure", inactivity_cop_lock_acquire_failure_stat},
    {"proxy.process.net.net_handler_run", net_handler_run_stat},
    {"proxy.process.net.read_bytes", net_read_bytes_stat},
    {"proxy.process.net.write_bytes", net_write_bytes_stat},
    {"proxy.process.net.fastopen_out.attempts", net_fastopen_attempts_stat},
    {"proxy.process.net.fastopen_out.successes", net_fastopen_successes_stat},
    {"proxy.process.socks.connections_successful", socks_connections_successful_stat},
    {"proxy.process.socks.connections_unsuccessful", socks_connections_unsuccessful_stat},
  };

  const std::pair<const char *, Net_Stats> non_persistent[] = {
    {"proxy.process.net.accepts_currently_open", net_accepts_currently_open_stat},
    {"proxy.process.net.connections_currently_open", net_connections_currently_open_stat},
    {"proxy.process.net.default_inactivity_timeout_applied", default_inactivity_timeout_stat},
    {"proxy.process.net.dynamic_keep_alive_timeout_in_count", keep_alive_queue_timeout_count_stat},
    {"proxy.process.net.dynamic_keep_alive_timeout_in_total", keep_alive_queue_timeout_total_stat},
    {"proxy.process.socks.connections_currently_open", socks_connections_currently_open_stat},
  };

  for (auto &p : persistent) {
    RecRegisterRawStat(net_rsb, RECT_PROCESS, p.first, RECD_INT, RECP_PERSISTENT, p.second, RecRawStatSyncSum);
  }

  for (auto &p : non_persistent) {
    RecRegisterRawStat(net_rsb, RECT_PROCESS, p.first, RECD_INT, RECP_NON_PERSISTENT, p.second, RecRawStatSyncSum);
  }

  NET_CLEAR_DYN_STAT(net_handler_run_stat);
  NET_CLEAR_DYN_STAT(net_connections_currently_open_stat);
  NET_CLEAR_DYN_STAT(net_accepts_currently_open_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_readfromnet_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_readfromnet_afterpoll_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_read_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_read_nodata_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_writetonet_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_writetonet_afterpoll_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_write_stat);
  NET_CLEAR_DYN_STAT(net_calls_to_write_nodata_stat);
  NET_CLEAR_DYN_STAT(socks_connections_currently_open_stat);
  NET_CLEAR_DYN_STAT(keep_alive_queue_timeout_total_stat);
  NET_CLEAR_DYN_STAT(keep_alive_queue_timeout_count_stat);
  NET_CLEAR_DYN_STAT(default_inactivity_timeout_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.tcp.total_accepts", RECD_INT, RECP_NON_PERSISTENT,
                     static_cast<int>(net_tcp_accept_stat), RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_tcp_accept_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.connections_throttled_in", RECD_INT, RECP_PERSISTENT,
                     (int)net_connections_throttled_in_stat, RecRawStatSyncSum);
  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.connections_throttled_out", RECD_INT, RECP_PERSISTENT,
                     (int)net_connections_throttled_out_stat, RecRawStatSyncSum);
}

void
ink_net_init(ts::ModuleVersion version)
{
  static int init_called = 0;

  ink_release_assert(version.check(NET_SYSTEM_MODULE_INTERNAL_VERSION));

  if (!init_called) {
    // do one time stuff
    // create a stat block for NetStats
    net_rsb = RecAllocateRawStatBlock((int)Net_Stat_Count);
    configure_net();
    register_net_stats();
  }

  init_called = 1;
}
