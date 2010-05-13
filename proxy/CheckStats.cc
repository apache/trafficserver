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

/***************************************************************************
 check_stats() - CheckStats.cc

 This function has a bunch of ink_assert calls which are supposed to
 signal bogus stat values. The function initially had many more asserts
 than it does now. This is because the completely ayshronous nature of
 the execution model forbids most of these asserts from being of use.

 This is also partly as a consequence of the fact that this function
 is called from within the SnapShotsContinuation function, which may
 execute at any time. So since the asserts may fire between consecutive
 stat updates, the asserts may not hold.


 ***************************************************************************/
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */
#include "Error.h"
#include "Stats.h"


void check_stats(void);

void
check_stats()
{
  return;

  /* IO Subsystem */
  /* ok, this may not be true if free is called for more smaller chunks than the alloc calls. */
//     debug_tag_assert("checkstats", STATCOUNT(IO_free_bytes) <= STATCOUNT(IO_alloc_bytes));
//     debug_tag_assert("checkstats", STATSUM(IO_free_bytes) <= STATSUM(IO_alloc_bytes));

  /* ok, this may not be true if free is called for more smaller chunks than the alloc calls. */
//     debug_tag_assert("checkstats", STATCOUNT(IO_free_buffers) <= STATCOUNT(IO_alloc_buffers));
  if (!(STATSUM(IO_free_buffers) <= STATSUM(IO_alloc_buffers)))
    syslog(LOG_WARNING, "SUM(IO_free_buffers) > SUM(IO_alloc_buffers)");


  /* HostDB Subsystem */
  /* this may not hold because other_hits is also incremented in probe_event */
//     debug_tag_assert("checkstats", STATCOUNT(hostdb_total_lookups) == (STATCOUNT(hostdb_level1_hits) +
//                                                                     STATCOUNT(hostdb_level1_locked) +
//                                                                     STATCOUNT(hostdb_other_hits)));

  /* Disk Subsystem */
  if (!(STATCOUNT(disk_connections_openned) >= 0))
    syslog(LOG_WARNING, "Disk Subsystem: COUNT(disk_connections_openned) < 0");

//     debug_tag_assert("checkstats", STATCOUNT(disk_connections_op_time) <= STATCOUNT(disk_connections_total_time));
//     debug_tag_assert("checkstats", STATSUM(disk_connections_op_time) <= STATSUM(disk_connections_total_time));
//     debug_tag_assert("checkstats", STATCOUNT(disk_one_shots_op_time) <= STATCOUNT(disk_one_shots_total_time));
//     debug_tag_assert("checkstats", STATSUM(disk_one_shots_op_time) <= STATSUM(disk_one_shots_total_time));


  /* Net Subsystem */
  if (!(STATCOUNT(net_connections_openned) >= 0))
    syslog(LOG_WARNING, "Net Subsystem: COUNT(net_connectioned_openned) < 0");

//     debug_tag_assert("checkstats", STATCOUNT(net_connections_op_time) <= STATCOUNT(net_connections_total_time));
//     debug_tag_assert("checkstats", STATSUM(net_connections_op_time) <= STATSUM(net_connections_total_time));
//     debug_tag_assert("checkstats", STATCOUNT(net_accepts_openned) >= STATCOUNT(net_accepts_closed));


  /* Cluster Subsystem */
//     debug_tag_assert("checkstats", STATCOUNT(cluster_connections_openned) >= STATCOUNT(cluster_con_total_time));
//     debug_tag_assert("checkstats", STATCOUNT(cluster_ctrl_msgs_send_time) >= STATCOUNT(cluster_slow_ctrl_msgs_sent));
//     debug_tag_assert("checkstats", STATCOUNT(cluster_ctrl_msgs_recv_time) >= STATCOUNT(cluster_slow_ctrl_msgs_recvd));
  if (!(STATCOUNT(machines_freed) <= STATCOUNT(machines_allocated))) {
    syslog(LOG_WARNING, "Cluster Subsystem: COUNT(machines_freed) > COUNT(machines_allocated)");
  }


  /* Cache Subystem */
  if (!(STATCOUNT(cache_read_active) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_read_active < 0");
  if (!(STATCOUNT(cache_write_active) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_write_active < 0");
  if (!(STATCOUNT(cache_connections_opened) >= STATCOUNT(cache_connections_closed)))
    syslog(LOG_WARNING, "Cache Subsystem: COUNT(cache_connections_openned) < COUNT(cache_connections_closed)");
  if (!(STATCOUNT(cache_bytes_free) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_bytes_free < 0");
  if (!(STATCOUNT(cache_bytes_free) <= STATCOUNT(cache_bytes_total)))
    syslog(LOG_WARNING, "Cache Subsystem: COUNT(cache_bytes_free) > COUNT(cache_bytes_total)");
  if (!(STATCOUNT(cache_bytes_deleted) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_bytes_deleted < 0");

  // pm said to take this one out for now.
// //   debug_tag_assert("checkstats", STATCOUNT(cache_bytes_inactive) >= 0);
  if (!(STATCOUNT(cache_bytes_active_read) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_bytes_active_read < 0");
  if (!(STATCOUNT(cache_bytes_active_write) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_bytes_active_write < 0");
  if (!(STATCOUNT(cache_num_pending) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_num_pending < 0");
  if (!(STATCOUNT(cache_num_active) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_num_active < 0");
  if (!(STATCOUNT(cache_ht_read_active) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_ht_read_active < 0");
  if (!(STATCOUNT(cache_ht_write_active) >= 0))
    syslog(LOG_WARNING, "Cache Subsystem: cache_ht_write_active < 0");


  /* GC Stats */
  if (STATCOUNT(gc_percent_full) > 0) {
    if (!((STATSUM(gc_percent_full) / STATCOUNT(gc_percent_full)) >= 0 &&
          (STATSUM(gc_percent_full) / STATCOUNT(gc_percent_full)) <= 100))
      syslog(LOG_WARNING, "GC Stats: SUM(gc_percent_full)/COUNT(gc_percent_full) not between 0 and 100");
  }


  /* Http Engine */
  /* http - connections count */
  /* hey - fix this! */
//   debug_tag_assert("checkstats", STATCOUNT(open_time) == (STATCOUNT(http_stats_user_agent_connection_start) +
//                                                        STATCOUNT(http_stats_origin_server_connection_start) +
//                                                        STATCOUNT(http_stats_parent_proxy_connection_start)));
  if (!(STATCOUNT(http_stats_user_agent_connections_current_count) >= 0))
    syslog(LOG_WARNING, "Http Engine: user_agent_coonections_current_count < 0");
  if (!(STATCOUNT(http_stats_origin_server_connections_current_count) >= 0))
    syslog(LOG_WARNING, "Http Engine: origin_server_connections_current_count < 0");
  if (!(STATCOUNT(http_stats_parent_proxy_connections_current_count) >= 0))
    syslog(LOG_WARNING, "Http Engine: parent_proxy_connections_current_count < 0");
  if (!(STATCOUNT(http_stats_cache_connections_current_count) >= 0))
    syslog(LOG_WARNING, "Http Engine: cache_connections_current_count < 0");
  if (!(STATCOUNT(http_stats_user_agent_connection_start) >=
        STATCOUNT(http_stats_user_agent_connections_current_count)))
    syslog(LOG_WARNING,
           "Http Engine: COUNT(user_agent_connection_start) < COUNT(user_agent_connections_current_count)");
  if (!
      (STATCOUNT(http_stats_origin_server_connection_start) >=
       STATCOUNT(http_stats_origin_server_connections_current_count)))
    syslog(LOG_WARNING,
           "Http Engine: COUNT(origin_server_connection_start) < COUNT(origin_server_connections_current_count)");
  if (!
      (STATCOUNT(http_stats_parent_proxy_connection_start) >=
       STATCOUNT(http_stats_parent_proxy_connections_current_count)))
    syslog(LOG_WARNING,
           "Http Engine: COUNT(parent_proxy_connection_start) < COUNT(parent_proxy_connections_current_count)");
  if (!(STATCOUNT(http_stats_cache_connection_start) >= STATCOUNT(http_stats_cache_connections_current_count)))
    syslog(LOG_WARNING, "Http Engine: COUNT(cache_connection_start) < COUNT(cache_connections_current_count)");

  /* http - transactions count */
//   debug_tag_assert("checkstats", STATCOUNT(http_stats_user_agent_transactions_start) >=
//           STATCOUNT(http_stats_user_agent_connection_start));
//   debug_tag_assert("checkstats", STATCOUNT(http_stats_origin_server_transactions_start) >=
//           STATCOUNT(http_stats_origin_server_connection_start));
//   debug_tag_assert("checkstats", STATCOUNT(http_stats_parent_proxy_transactions_start) >=
//           STATCOUNT(http_stats_parent_proxy_connection_start));
  if (!(STATCOUNT(http_stats_user_agent_transactions_current_count) >= 0))
    syslog(LOG_WARNING, "Http Transactions: user_agent_transactions_current_count < 0");
  if (!(STATCOUNT(http_stats_origin_server_transactions_current_count) >= 0))
    syslog(LOG_WARNING, "Http Transactions: origin_server_transactions_current_count < 0");
  if (!(STATCOUNT(http_stats_parent_proxy_transactions_current_count) >= 0))
    syslog(LOG_WARNING, "Http Transactions: parent_proxy_transactions_current_count < 0");

  /* http - connection time */
  /* these may not hold because total_connection_time is updated when connection terminates */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_user_agent_connection_time) ==  */
/* 	     STATCOUNT(http_stats_user_agent_connection_start)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_origin_server_connection_time) ==  */
/* 	     STATCOUNT(http_stats_origin_server_connection_start)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_parent_proxy_connection_time) ==  */
/* 	     STATCOUNT(http_stats_parent_proxy_connection_start)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_cache_connection_time) ==  */
/* 	     STATCOUNT(http_stats_cache_connection_start)); */

  /* http - transaction time */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_user_agent_transactions_time) >=  */
/* 	     STATCOUNT(http_stats_user_agent_connection_start)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_origin_server_transactions_time) >=  */
/* 	     STATCOUNT(http_stats_origin_server_connection_start)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_parent_proxy_transactions_time) >=  */
/* 	     STATCOUNT(http_stats_parent_proxy_connection_start)); */
/*   debug_tag_assert("checkstats", STATSUM(http_stats_total_user_agent_transactions_time) >=  */
/* 	     (STATSUM(http_stats_total_origin_server_connection_start) +  */
/* 	      STATSUM(http_stats_total_parent_proxy_connection_start))); */
/*   debug_tag_assert("checkstats", STATSUM(http_stats_total_user_agent_transactions_time) >=  */
/*              STATSUM(http_stats_total_user_agent_connection_time)); */

  /* http - document size */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_request_document_total_size) == */
/* 	     STATCOUNT(http_stats_user_agent_request_document_total_size)); */
/*   debug_tag_assert("checkstats", STATSUM(http_stats_request_document_total_size) == */
/* 	     STATSUM(http_stats_user_agent_request_document_total_size)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_request_document_total_size) >= */
/* 	     (STATCOUNT(http_stats_origin_server_request_document_total_size) + */
/* 	      STATCOUNT(http_stats_parent_proxy_request_document_total_size))); */
/*   debug_tag_assert("checkstats", STATSUM(http_stats_request_document_total_size) >= */
/* 	     (STATSUM(http_stats_origin_server_request_document_total_size) + */
/* 	      STATSUM(http_stats_parent_proxy_request_document_total_size))); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_response_document_total_size) == */
/* 	     STATCOUNT(http_stats_user_agent_response_document_total_size)); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_response_document_total_size) >= */
/* 	     (STATCOUNT(http_stats_origin_server_response_document_total_size) + */
/* 	      STATCOUNT(http_stats_parent_proxy_response_document_total_size))); */
/*   debug_tag_assert("checkstats", STATSUM(http_stats_response_document_total_size) >= */
/* 	     (STATSUM(http_stats_origin_server_response_document_total_size) + */
/* 	      STATSUM(http_stats_parent_proxy_response_document_total_size))); */

  /* http - connection speed */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_user_agent_transactions_time) ==  */
/* 	     (STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_100) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_1K) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_10K) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_100K) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_1M) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_10M) +  */
/* 	      STATCOUNT(http_stats_user_agent_speed_bytes_per_sec_100M))); */
/*   debug_tag_assert("checkstats", STATCOUNT(http_stats_total_origin_server_transactions_time) ==  */
/* 	     (STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_100) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_1K) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_10K) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_100K) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_1M) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_10M) +  */
/* 	      STATCOUNT(http_stats_origin_server_speed_bytes_per_sec_100M))); */
}

// /* Proposed new stats */
// STAT(Sum, dns_lookups);


// /* Reinitialization and equalization of stats at startup */
// EQUATE_AT_STARTUP(IO_alloc_bytes, IO_free_bytes);
// EQUATE_AT_STARTUP(IO_alloc_buffers, IO_free_buffers);


// RESET_AT_STARTUP(dns_queue_length);
