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

#ifndef _P_CLUSTER_H__
#define _P_CLUSTER_H__

#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "I_RecProcess.h"
#include "P_Net.h"
#include "P_Cache.h"

#ifdef HTTP_CACHE
#include "MIME.h"
#include "HTTP.h"
// #include "HttpTransactCache.h"
#endif

#include "P_ClusterMachine.h"
#include "P_ClusterCache.h"
#include "P_ClusterCacheInternal.h"
#include "P_ClusterInternal.h"
#include "P_ClusterHandler.h"
#include "P_ClusterInline.h"
#include "P_ClusterLib.h"
#include "P_ClusterLoadMonitor.h"
#include "P_TimeTrace.h"

#define ECLUSTER_NO_VC (CLUSTER_ERRNO + 0)
#define ECLUSTER_NO_MACHINE (CLUSTER_ERRNO + 1)
#define ECLUSTER_OP_TIMEOUT (CLUSTER_ERRNO + 2)
#define ECLUSTER_ORB_DATA_READ (CLUSTER_ERRNO + 3)
#define ECLUSTER_ORB_EIO (CLUSTER_ERRNO + 4)
#define ECLUSTER_CHANNEL_INUSE (CLUSTER_ERRNO + 5)
#define ECLUSTER_NOMORE_CHANNELS (CLUSTER_ERRNO + 6)

int init_clusterprocessor(void);
enum {
  CLUSTER_CONNECTIONS_OPEN_STAT,
  CLUSTER_CONNECTIONS_OPENNED_STAT,
  CLUSTER_CON_TOTAL_TIME_STAT,
  CLUSTER_CTRL_MSGS_SENT_STAT,
  CLUSTER_SLOW_CTRL_MSGS_SENT_STAT,
  CLUSTER_CTRL_MSGS_RECVD_STAT,
  CLUSTER_SLOW_CTRL_MSGS_RECVD_STAT,
  CLUSTER_CTRL_MSGS_SEND_TIME_STAT,
  CLUSTER_CTRL_MSGS_RECV_TIME_STAT,
  CLUSTER_READ_BYTES_STAT,
  CLUSTER_WRITE_BYTES_STAT,
  CLUSTER_OP_DELAYED_FOR_LOCK_STAT,
  CLUSTER_CONNECTIONS_READ_LOCKED_STAT,
  CLUSTER_CONNECTIONS_WRITE_LOCKED_STAT,
  CLUSTER_CONNECTIONS_BUMPED_STAT,
  CLUSTER_NODES_STAT,
  CLUSTER_NET_BACKUP_STAT,
  CLUSTER_MACHINES_ALLOCATED_STAT,
  CLUSTER_MACHINES_FREED_STAT,
  CLUSTER_CONFIGURATION_CHANGES_STAT,
  CLUSTER_DELAYED_READS_STAT,
  CLUSTER_BYTE_BANK_USED_STAT,
  CLUSTER_ALLOC_DATA_NEWS_STAT,
  CLUSTER_WRITE_BB_MALLOCS_STAT,
  CLUSTER_PARTIAL_READS_STAT,
  CLUSTER_PARTIAL_WRITES_STAT,
  CLUSTER_CACHE_OUTSTANDING_STAT,
  CLUSTER_REMOTE_OP_TIMEOUTS_STAT,
  CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT,
  CLUSTER_CHAN_INUSE_STAT,
  CLUSTER_OPEN_DELAYS_STAT,
  CLUSTER_OPEN_DELAY_TIME_STAT,
  CLUSTER_CACHE_CALLBACKS_STAT,
  CLUSTER_CACHE_CALLBACK_TIME_STAT,
  CLUSTER_THREAD_STEAL_EXPIRES_STAT,
  CLUSTER_RDMSG_ASSEMBLE_TIME_STAT,
  CLUSTER_PING_TIME_STAT,
  cluster_setdata_no_CLUSTERVC_STAT,
  CLUSTER_SETDATA_NO_TUNNEL_STAT,
  CLUSTER_SETDATA_NO_CACHEVC_STAT,
  cluster_setdata_no_CLUSTER_STAT,
  CLUSTER_VC_WRITE_STALL_STAT,
  CLUSTER_NO_REMOTE_SPACE_STAT,
  CLUSTER_LEVEL1_BANK_STAT,
  CLUSTER_MULTILEVEL_BANK_STAT,
  CLUSTER_VC_CACHE_INSERT_LOCK_MISSES_STAT,
  CLUSTER_VC_CACHE_INSERTS_STAT,
  CLUSTER_VC_CACHE_LOOKUP_LOCK_MISSES_STAT,
  CLUSTER_VC_CACHE_LOOKUP_HITS_STAT,
  CLUSTER_VC_CACHE_LOOKUP_MISSES_STAT,
  CLUSTER_VC_CACHE_SCANS_STAT,
  CLUSTER_VC_CACHE_SCAN_LOCK_MISSES_STAT,
  CLUSTER_VC_CACHE_PURGES_STAT,
  CLUSTER_WRITE_LOCK_MISSES_STAT,
  CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT,
  CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT,
  CLUSTER_LOCAL_CONNECTION_TIME_STAT,
  CLUSTER_REMOTE_CONNECTION_TIME_STAT,
  CLUSTER_SETDATA_NO_CLUSTERVC_STAT,
  CLUSTER_SETDATA_NO_CLUSTER_STAT,
  CLUSTER_VC_READ_LIST_LEN_STAT,
  CLUSTER_VC_WRITE_LIST_LEN_STAT,
  cluster_stat_count
};

extern RecRawStatBlock *cluster_rsb;
#define CLUSTER_INCREMENT_DYN_STAT(x) RecIncrRawStat(cluster_rsb, mutex->thread_holding, (int)x, 1);
#define CLUSTER_DECREMENT_DYN_STAT(x) RecIncrRawStat(cluster_rsb, mutex->thread_holding, (int)x, -1);
#define CLUSTER_SUM_DYN_STAT(x, y) RecIncrRawStat(cluster_rsb, mutex->thread_holding, (int)x, y);
#define CLUSTER_SUM_GLOBAL_DYN_STAT(x, y) RecIncrGlobalRawStatSum(cluster_rsb, x, y)
#define CLUSTER_CLEAR_DYN_STAT(x)          \
  do {                                     \
    RecSetRawStatSum(cluster_rsb, x, 0);   \
    RecSetRawStatCount(cluster_rsb, x, 0); \
  } while (0);

#endif
