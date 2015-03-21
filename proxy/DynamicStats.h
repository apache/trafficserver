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

_HEADER
//
// Dynamic http stats
//
// this one's in the state machine currently
//
//
// Dynamic Cluster Stats
//
_D(cluster_connections_open_stat)
_D(cluster_connections_openned_stat)
_D(cluster_con_total_time_stat)
_D(cluster_ctrl_msgs_sent_stat)
_D(cluster_slow_ctrl_msgs_sent_stat) // fast ctrl messages do not require a mallo
_D(cluster_ctrl_msgs_recvd_stat)
_D(cluster_slow_ctrl_msgs_recvd_stat)
_D(cluster_ctrl_msgs_send_time_stat)
_D(cluster_ctrl_msgs_recv_time_stat)
_D(cluster_read_bytes_stat)
_D(cluster_write_bytes_stat)
_D(cluster_op_delayed_for_lock_stat) // a message to a machine was blocked by a locked connection
_D(cluster_connections_locked_stat)  // a connection could not use its slot (locked)
_D(cluster_connections_bumped_stat)  // a connection could not get a slot (scheduled too late)
_D(cluster_nodes_stat)
_D(cluster_net_backup_stat)
_D(cluster_machines_allocated_stat)
_D(cluster_machines_freed_stat)
_D(cluster_configuration_changes_stat)
_D(cluster_delayed_reads_stat)
_D(cluster_byte_bank_used_stat)
_D(cluster_alloc_data_news_stat)
_D(cluster_write_bb_mallocs_stat)
_D(cluster_partial_reads_stat)
_D(cluster_partial_writes_stat)
_D(cluster_cache_outstanding_stat)
_D(cluster_remote_op_timeouts_stat)
_D(cluster_remote_op_reply_timeouts_stat)
_D(cluster_chan_inuse_stat)
_D(cluster_open_delays_stat)
_D(cluster_open_delay_time_stat)
_D(cluster_cache_callbacks_stat)
_D(cluster_cache_callback_time_stat)
_D(cluster_cache_rmt_callbacks_stat)
_D(cluster_cache_rmt_callback_time_stat)
_D(cluster_cache_lkrmt_callbacks_stat)
_D(cluster_cache_lkrmt_callback_time_stat)
_D(cluster_thread_steal_expires_stat)
_D(cluster_local_connections_closed_stat)
_D(cluster_local_connection_time_stat)
_D(cluster_remote_connections_closed_stat)
_D(cluster_remote_connection_time_stat)
_D(cluster_rdmsg_assemble_time_stat)
_D(cluster_ping_time_stat)
_D(cluster_setdata_no_clustervc_stat)
_D(cluster_setdata_no_tunnel_stat)
_D(cluster_setdata_no_cachevc_stat)
_D(cluster_setdata_no_cluster_stat)
_D(cluster_vc_write_stall_stat)
_D(cluster_no_remote_space_stat)
_D(cluster_level1_bank_stat)
_D(cluster_multilevel_bank_stat)
_D(cluster_vc_cache_insert_lock_misses_stat)
_D(cluster_vc_cache_inserts_stat)
_D(cluster_vc_cache_lookup_lock_misses_stat)
_D(cluster_vc_cache_lookup_hits_stat)
_D(cluster_vc_cache_lookup_misses_stat)
_D(cluster_vc_cache_scans_stat)
_D(cluster_vc_cache_scan_lock_misses_stat)
_D(cluster_vc_cache_purges_stat)
_D(cluster_write_lock_misses_stat)


//
// Dynamic Load Shedding Stats
//
_D(cpu_metric_load_percent_stat)
_D(cpu_metric_net_loops_per_second_stat)
_D(cpu_metric_fds_ready_per_loop_stat)

_FOOTER
