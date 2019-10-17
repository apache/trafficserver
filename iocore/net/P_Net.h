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

// Net Stats

enum Net_Stats {
  net_handler_run_stat,
  net_read_bytes_stat,
  net_write_bytes_stat,
  net_connections_currently_open_stat,
  net_accepts_currently_open_stat,
  net_calls_to_readfromnet_stat,
  net_calls_to_readfromnet_afterpoll_stat,
  net_calls_to_read_stat,
  net_calls_to_read_nodata_stat,
  net_calls_to_writetonet_stat,
  net_calls_to_writetonet_afterpoll_stat,
  net_calls_to_write_stat,
  net_calls_to_write_nodata_stat,
  socks_connections_successful_stat,
  socks_connections_unsuccessful_stat,
  socks_connections_currently_open_stat,
  inactivity_cop_lock_acquire_failure_stat,
  keep_alive_queue_timeout_total_stat,
  keep_alive_queue_timeout_count_stat,
  default_inactivity_timeout_stat,
  net_fastopen_attempts_stat,
  net_fastopen_successes_stat,
  net_tcp_accept_stat,
  net_connections_throttled_in_stat,
  net_connections_throttled_out_stat,
  Net_Stat_Count
};

struct RecRawStatBlock;
extern RecRawStatBlock *net_rsb;
#define SSL_HANDSHAKE_WANT_READ 6
#define SSL_HANDSHAKE_WANT_WRITE 7
#define SSL_HANDSHAKE_WANT_ACCEPT 8
#define SSL_HANDSHAKE_WANT_CONNECT 9

#define NET_INCREMENT_DYN_STAT(_x) RecIncrRawStatSum(net_rsb, mutex->thread_holding, (int)_x, 1)

#define NET_DECREMENT_DYN_STAT(_x) RecIncrRawStatSum(net_rsb, mutex->thread_holding, (int)_x, -1)

#define NET_SUM_DYN_STAT(_x, _r) RecIncrRawStatSum(net_rsb, mutex->thread_holding, (int)_x, _r)

#define NET_READ_DYN_SUM(_x, _sum) RecGetRawStatSum(net_rsb, (int)_x, &_sum)

#define NET_READ_DYN_STAT(_x, _count, _sum)        \
  do {                                             \
    RecGetRawStatSum(net_rsb, (int)_x, &_sum);     \
    RecGetRawStatCount(net_rsb, (int)_x, &_count); \
  } while (0)

#define NET_CLEAR_DYN_STAT(x)          \
  do {                                 \
    RecSetRawStatSum(net_rsb, x, 0);   \
    RecSetRawStatCount(net_rsb, x, 0); \
  } while (0);

// For global access
#define NET_SUM_GLOBAL_DYN_STAT(_x, _r) RecIncrGlobalRawStatSum(net_rsb, (_x), (_r))
#define NET_READ_GLOBAL_DYN_SUM(_x, _sum) RecGetGlobalRawStatSum(net_rsb, _x, &_sum)

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

#include "P_SSLNetVConnection.h"
#include "P_SSLNetProcessor.h"
#include "P_SSLNetAccept.h"
#include "P_SSLCertLookup.h"

#if TS_USE_QUIC == 1
#include "P_QUICNetVConnection.h"
#include "P_QUICNetProcessor.h"
#include "P_QUICPacketHandler.h"
#include "P_QUICNet.h"
#endif

static constexpr ts::ModuleVersion NET_SYSTEM_MODULE_INTERNAL_VERSION(NET_SYSTEM_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE);

// For very verbose iocore debugging.
#ifndef DEBUG
#define NetDebug(tag, fmt, ...)
#else
#define NetDebug(tag, fmt, ...) Debug(tag, fmt, ##__VA_ARGS__)
#endif

/// Default amount of buffer space to use for the initial read on an incoming connection.
/// This is an IOBufferBlock index, not the size in bytes.
static size_t const CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX = BUFFER_SIZE_INDEX_4K;
