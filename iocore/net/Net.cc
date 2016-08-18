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

RecRawStatBlock *net_rsb = NULL;

// All in milli-seconds
int net_config_poll_timeout = -1; // This will get set via either command line or records.config.
int net_event_period        = 10;
int net_accept_period       = 10;
int net_retry_delay         = 10;
int net_throttle_delay      = 50; /* milliseconds */

static inline void
configure_net(void)
{
  REC_RegisterConfigUpdateFunc("proxy.config.net.connections_throttle", change_net_connections_throttle, NULL);
  REC_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");

  REC_EstablishStaticConfigInt32(net_retry_delay, "proxy.config.net.retry_delay");
  REC_EstablishStaticConfigInt32(net_throttle_delay, "proxy.config.net.throttle_delay");

  // These are not reloadable
  REC_ReadConfigInteger(net_event_period, "proxy.config.net.event_period");
  REC_ReadConfigInteger(net_accept_period, "proxy.config.net.accept_period");
}

static inline void
register_net_stats()
{
  //
  // Register statistics
  //
  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.net_handler_run", RECD_INT, RECP_PERSISTENT,
                     (int)net_handler_run_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_handler_run_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.read_bytes", RECD_INT, RECP_PERSISTENT, (int)net_read_bytes_stat,
                     RecRawStatSyncSum);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.write_bytes", RECD_INT, RECP_PERSISTENT, (int)net_write_bytes_stat,
                     RecRawStatSyncSum);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.connections_currently_open", RECD_INT, RECP_NON_PERSISTENT,
                     (int)net_connections_currently_open_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_connections_currently_open_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.accepts_currently_open", RECD_INT, RECP_NON_PERSISTENT,
                     (int)net_accepts_currently_open_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_accepts_currently_open_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_readfromnet", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_readfromnet_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_readfromnet_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_readfromnet_afterpoll", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_readfromnet_afterpoll_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_readfromnet_afterpoll_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_read", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_read_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_read_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_read_nodata", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_read_nodata_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_read_nodata_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_writetonet", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_writetonet_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_writetonet_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_writetonet_afterpoll", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_writetonet_afterpoll_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_writetonet_afterpoll_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_write", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_write_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_write_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.calls_to_write_nodata", RECD_INT, RECP_PERSISTENT,
                     (int)net_calls_to_write_nodata_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(net_calls_to_write_nodata_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.socks.connections_successful", RECD_INT, RECP_PERSISTENT,
                     (int)socks_connections_successful_stat, RecRawStatSyncSum);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.socks.connections_unsuccessful", RECD_INT, RECP_PERSISTENT,
                     (int)socks_connections_unsuccessful_stat, RecRawStatSyncSum);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.socks.connections_currently_open", RECD_INT, RECP_NON_PERSISTENT,
                     (int)socks_connections_currently_open_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(socks_connections_currently_open_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.inactivity_cop_lock_acquire_failure", RECD_INT, RECP_PERSISTENT,
                     (int)inactivity_cop_lock_acquire_failure_stat, RecRawStatSyncSum);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.dynamic_keep_alive_timeout_in_total", RECD_INT, RECP_NON_PERSISTENT,
                     (int)keep_alive_queue_timeout_total_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(keep_alive_queue_timeout_total_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.dynamic_keep_alive_timeout_in_count", RECD_INT, RECP_NON_PERSISTENT,
                     (int)keep_alive_queue_timeout_count_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(keep_alive_queue_timeout_count_stat);

  RecRegisterRawStat(net_rsb, RECT_PROCESS, "proxy.process.net.default_inactivity_timeout_applied", RECD_INT, RECP_NON_PERSISTENT,
                     (int)default_inactivity_timeout_stat, RecRawStatSyncSum);
  NET_CLEAR_DYN_STAT(default_inactivity_timeout_stat);
}

void
ink_net_init(ModuleVersion version)
{
  static int init_called = 0;

  ink_release_assert(!checkModuleVersion(version, NET_SYSTEM_MODULE_VERSION));
  if (!init_called) {
    // do one time stuff
    // create a stat block for NetStats
    net_rsb = RecAllocateRawStatBlock((int)Net_Stat_Count);
    configure_net();
    register_net_stats();
  }

  init_called = 1;
}
