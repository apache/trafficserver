.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

============
traffic_line
============

Synopsis
========


:program:`traffic_line` [options]

.. _traffic-line-commands:

Description
===========

:program:`traffic_line` is used to execute individual Traffic Server
commands and to script multiple commands in a shell.

Options
=======

.. program:: traffic_line

.. option:: -B, --bounce_cluster

    Bounce all Traffic Server nodes in the cluster. Bouncing Traffic
    Server shuts down and immediately restarts Traffic Server,
    node-by-node.

.. option:: -b, --bounce_local

    Bounce Traffic Server on the local node. Bouncing Traffic Server
    shuts down and immediately restarts the Traffic Server node.

.. option:: -C, --clear_cluster

    Clears accumulated statistics on all nodes in the cluster.

.. option:: -c, --clear_node

    Clears accumulated statistics on the local node.

.. option:: -h, --help

    Print usage information and exit.

.. option:: -L, --restart_local

    Restart the :program:`traffic_manager` and :program:`traffic_server`
    processes on the local node.

.. option:: -M, --restart_cluster

    Restart the :program:`traffic_manager` process and the
    :program:`traffic_server` process on all the nodes in a cluster.

.. option:: -r VAR, --read_var VAR

    Display specific performance statistics or a current configuration
    setting.

.. option:: -s VAR, --set_var VAR

    Set the configuration variable named `VAR`. The value of the configuration
    variable is given by the :option:`traffic_line -v` option.
    Refer to the :file:`records.config` documentation for a list
    of the configuration variables you can specify.

.. option:: -S, --shutdown

    Shut down Traffic Server on the local node.

.. option:: -U, --startup

    Start Traffic Server on the local node.

.. option:: -v VALUE, --value VALUE

    Specify the value to set when setting a configuration variable.

.. option:: -V, --version

    Print version information and exit.

.. option:: -x, --reread_config

    Initiate a Traffic Server configuration file reread. Use this
    command to update the running configuration after any configuration
    file modification.

.. option:: -Z, --zero_cluster

    Reset performance statistics to zero across the cluster.

.. option:: -z, --zero_node

    Reset performance statistics to zero on the local node.

.. option:: --offline PATH

   Mark a cache storage device as offline. The storage is identified by a *path* which must match exactly a path
   specified in :file:`storage.config`. This removes the storage from the cache and redirects requests that would have
   used this storage to other storage. This has exactly the same effect as a disk failure for that storage. This does
   not persist across restarts of the :program:`traffic_server` process.

.. option:: --alarms

   List all alarm events that have not been acknowledged (cleared).

.. option:: --clear_alarms [all | #event | name]

   Clear (acknowledge) an alarm event. The arguments are "all" for all current
   alarms, a specific alarm number (e.g. ''1''), or an alarm string identifier
   (e.g. ''MGMT_ALARM_PROXY_CONFIG_ERROR'').

.. option:: --status

   Show the current proxy server status, indicating if we're running or not.


.. _traffic-line-performance-statistics:

Performance Statistics
======================

.. XXX: These variables need to be documented.. Volunteers? //igalic

The :option:`traffic_line -r` option accepts the following variable names::

    proxy.node.num_processes
    proxy.node.hostname_FQ
    proxy.node.hostname
    proxy.node.restarts.manager.start_time
    proxy.node.restarts.proxy.start_time
    proxy.node.restarts.proxy.stop_time
    proxy.node.restarts.proxy.restart_count
    proxy.node.version.manager.short
    proxy.node.version.manager.long
    proxy.node.version.manager.build_number
    proxy.node.version.manager.build_time
    proxy.node.version.manager.build_date
    proxy.node.version.manager.build_machine
    proxy.node.version.manager.build_person
    proxy.node.bandwidth_hit_ratio
    proxy.node.bandwidth_hit_ratio_int_pct
    proxy.node.hostdb.hit_ratio
    proxy.node.hostdb.hit_ratio_int_pct
    proxy.node.proxy_running
    proxy.node.cache.percent_free
    proxy.node.cache_hit_ratio
    proxy.node.cache_hit_ratio_int_pct
    proxy.node.cache_hit_ratio_avg_10s_int_pct
    proxy.node.bandwidth_hit_ratio_avg_10s_int_pct
    proxy.node.bandwidth_hit_ratio_avg_10s
    proxy.node.http.cache_hit_fresh_avg_10s
    proxy.node.http.cache_hit_revalidated_avg_10s
    proxy.node.http.cache_hit_ims_avg_10s
    proxy.node.http.cache_hit_stale_served_avg_10s
    proxy.node.http.cache_miss_cold_avg_10s
    proxy.node.http.cache_miss_changed_avg_10s
    proxy.node.http.cache_miss_not_cacheable_avg_10s
    proxy.node.http.cache_miss_client_no_cache_avg_10s
    proxy.node.http.cache_miss_ims_avg_10s
    proxy.node.http.cache_read_error_avg_10s
    proxy.node.cache_total_hits_avg_10s
    proxy.node.cache_total_misses_avg_10s
    proxy.node.cache_hit_ratio_avg_10s
    proxy.node.hostdb.total_lookups_avg_10s
    proxy.node.hostdb.total_hits_avg_10s
    proxy.node.hostdb.hit_ratio_avg_10s
    proxy.node.http.transaction_counts_avg_10s.hit_fresh
    proxy.node.http.transaction_counts_avg_10s.hit_revalidated
    proxy.node.http.transaction_counts_avg_10s.miss_cold
    proxy.node.http.transaction_counts_avg_10s.miss_not_cacheable
    proxy.node.http.transaction_counts_avg_10s.miss_changed
    proxy.node.http.transaction_counts_avg_10s.miss_client_no_cache
    proxy.node.http.transaction_counts_avg_10s.errors.connect_failed
    proxy.node.http.transaction_counts_avg_10s.errors.aborts
    proxy.node.http.transaction_counts_avg_10s.errors.possible_aborts
    proxy.node.http.transaction_counts_avg_10s.errors.pre_accept_hangups
    proxy.node.http.transaction_counts_avg_10s.errors.early_hangups
    proxy.node.http.transaction_counts_avg_10s.errors.empty_hangups
    proxy.node.http.transaction_counts_avg_10s.errors.other
    proxy.node.http.transaction_counts_avg_10s.other.unclassified
    proxy.node.http.transaction_frac_avg_10s.hit_fresh
    proxy.node.http.transaction_frac_avg_10s.hit_revalidated
    proxy.node.http.transaction_frac_avg_10s.miss_cold
    proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable
    proxy.node.http.transaction_frac_avg_10s.miss_changed
    proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache
    proxy.node.http.transaction_frac_avg_10s.errors.connect_failed
    proxy.node.http.transaction_frac_avg_10s.errors.aborts
    proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts
    proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups
    proxy.node.http.transaction_frac_avg_10s.errors.early_hangups
    proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups
    proxy.node.http.transaction_frac_avg_10s.errors.other
    proxy.node.http.transaction_frac_avg_10s.other.unclassified
    proxy.node.http.transaction_frac_avg_10s.hit_fresh_int_pct
    proxy.node.http.transaction_frac_avg_10s.hit_revalidated_int_pct
    proxy.node.http.transaction_frac_avg_10s.miss_cold_int_pct
    proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable_int_pct
    proxy.node.http.transaction_frac_avg_10s.miss_changed_int_pct
    proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.connect_failed_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.aborts_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.early_hangups_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups_int_pct
    proxy.node.http.transaction_frac_avg_10s.errors.other_int_pct
    proxy.node.http.transaction_frac_avg_10s.other.unclassified_int_pct
    proxy.node.http.transaction_msec_avg_10s.hit_fresh
    proxy.node.http.transaction_msec_avg_10s.hit_revalidated
    proxy.node.http.transaction_msec_avg_10s.miss_cold
    proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable
    proxy.node.http.transaction_msec_avg_10s.miss_changed
    proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache
    proxy.node.http.transaction_msec_avg_10s.errors.connect_failed
    proxy.node.http.transaction_msec_avg_10s.errors.aborts
    proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts
    proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups
    proxy.node.http.transaction_msec_avg_10s.errors.early_hangups
    proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups
    proxy.node.http.transaction_msec_avg_10s.errors.other
    proxy.node.http.transaction_msec_avg_10s.other.unclassified
    proxy.node.http.throughput
    proxy.node.http.user_agent_xacts_per_second
    proxy.node.http.user_agent_current_connections_count
    proxy.node.http.user_agent_total_request_bytes
    proxy.node.http.user_agent_total_response_bytes
    proxy.node.http.user_agents_total_transactions_count
    proxy.node.http.user_agents_total_documents_served
    proxy.node.http.origin_server_current_connections_count
    proxy.node.http.origin_server_total_request_bytes
    proxy.node.http.origin_server_total_response_bytes
    proxy.node.http.parent_proxy_total_request_bytes
    proxy.node.http.parent_proxy_total_response_bytes
    proxy.node.http.origin_server_total_transactions_count
    proxy.node.http.cache_current_connections_count
    proxy.node.http.current_parent_proxy_connections
    proxy.node.cache.contents.num_docs
    proxy.node.cache.bytes_total
    proxy.node.cache.bytes_total_mb
    proxy.node.cache.bytes_free
    proxy.node.cache.bytes_free_mb
    proxy.node.cache.percent_free_int_pct
    proxy.node.cache_total_hits
    proxy.node.cache_total_misses
    proxy.node.current_cache_connections
    proxy.node.dns.lookups_per_second
    proxy.node.dns.lookup_avg_time_ms
    proxy.node.dns.total_dns_lookups
    proxy.node.hostdb.total_lookups
    proxy.node.hostdb.total_hits
    proxy.node.cluster.nodes
    proxy.node.client_throughput_out
    proxy.node.client_throughput_out_kbit
    proxy.node.current_client_connections
    proxy.node.current_server_connections
    proxy.node.user_agent_total_bytes
    proxy.node.origin_server_total_bytes
    proxy.node.user_agent_total_bytes_avg_10s
    proxy.node.origin_server_total_bytes_avg_10s
    proxy.node.user_agent_xacts_per_second
    proxy.node.user_agents_total_documents_served
    proxy.cluster.user_agent_total_bytes_avg_10s
    proxy.cluster.origin_server_total_bytes_avg_10s
    proxy.cluster.bandwidth_hit_ratio
    proxy.cluster.bandwidth_hit_ratio_int_pct
    proxy.cluster.bandwidth_hit_ratio_avg_10s
    proxy.cluster.http.throughput
    proxy.cluster.http.user_agent_xacts_per_second
    proxy.cluster.http.user_agent_current_connections_count
    proxy.cluster.http.user_agent_total_request_bytes
    proxy.cluster.http.user_agent_total_response_bytes
    proxy.cluster.http.user_agents_total_transactions_count
    proxy.cluster.http.user_agents_total_documents_served
    proxy.cluster.http.origin_server_current_connections_count
    proxy.cluster.http.origin_server_total_request_bytes
    proxy.cluster.http.origin_server_total_response_bytes
    proxy.cluster.http.origin_server_total_transactions_count
    proxy.cluster.http.cache_current_connections_count
    proxy.cluster.http.current_parent_proxy_connections
    proxy.cluster.http.parent_proxy_total_request_bytes
    proxy.cluster.http.parent_proxy_total_response_bytes
    proxy.cluster.cache.contents.num_docs
    proxy.cluster.cache.bytes_free
    proxy.cluster.cache.bytes_free_mb
    proxy.cluster.cache.percent_free
    proxy.cluster.cache.percent_free_int_pct
    proxy.cluster.cache_hit_ratio
    proxy.cluster.cache_hit_ratio_int_pct
    proxy.cluster.cache_total_hits
    proxy.cluster.cache_total_misses
    proxy.cluster.current_cache_connections
    proxy.cluster.cache_total_hits_avg_10s
    proxy.cluster.cache_total_misses_avg_10s
    proxy.cluster.cache_hit_ratio_avg_10s
    proxy.cluster.dns.lookups_per_second
    proxy.cluster.dns.total_dns_lookups
    proxy.cluster.hostdb.hit_ratio
    proxy.cluster.hostdb.hit_ratio_int_pct
    proxy.cluster.hostdb.total_lookups_avg_10s
    proxy.cluster.hostdb.total_hits_avg_10s
    proxy.cluster.hostdb.hit_ratio_avg_10s
    proxy.cluster.user_agent_xacts_per_second
    proxy.cluster.client_throughput_out
    proxy.cluster.client_throughput_out_kbit
    proxy.cluster.current_client_connections
    proxy.cluster.current_server_connections
    proxy.process.version.server.short
    proxy.process.version.server.long
    proxy.process.version.server.build_number
    proxy.process.version.server.build_time
    proxy.process.version.server.build_date
    proxy.process.version.server.build_machine
    proxy.process.version.server.build_person
    proxy.process.net.net_handler_run
    proxy.process.net.read_bytes
    proxy.process.net.write_bytes
    proxy.process.net.calls_to_readfromnet
    proxy.process.net.calls_to_readfromnet_afterpoll
    proxy.process.net.calls_to_read
    proxy.process.net.calls_to_read_nodata
    proxy.process.net.calls_to_writetonet
    proxy.process.net.calls_to_writetonet_afterpoll
    proxy.process.net.calls_to_write
    proxy.process.net.calls_to_write_nodata
    proxy.process.socks.connections_successful
    proxy.process.socks.connections_unsuccessful
    proxy.process.cache.read_per_sec
    proxy.process.cache.write_per_sec
    proxy.process.cache.KB_read_per_sec
    proxy.process.cache.KB_write_per_sec
    proxy.process.cache.ram_cache.total_bytes
    proxy.process.hostdb.total_entries
    proxy.process.hostdb.total_lookups
    proxy.process.hostdb.ttl
    proxy.process.hostdb.ttl_expires
    proxy.process.hostdb.re_dns_on_reload
    proxy.process.hostdb.bytes
    proxy.process.dns.total_dns_lookups
    proxy.process.dns.lookup_avg_time
    proxy.process.dns.lookup_successes
    proxy.process.dns.fail_avg_time
    proxy.process.dns.lookup_failures
    proxy.process.dns.retries
    proxy.process.dns.max_retries_exceeded
    proxy.process.log.bytes_buffered
    proxy.process.log.bytes_written_to_disk
    proxy.process.log.bytes_sent_to_network
    proxy.process.log.bytes_received_from_network
    proxy.process.log.event_log_error
    proxy.process.log.event_log_access
    proxy.process.log.event_log_access_fail
    proxy.process.log.event_log_access_skip
    proxy.process.cache.volume_0.ram_cache.total_bytes
    proxy.process.http.completed_requests
    proxy.process.http.total_incoming_connections
    proxy.process.http.total_client_connections
    proxy.process.http.total_client_connections_ipv4
    proxy.process.http.total_client_connections_ipv6
    proxy.process.http.total_server_connections
    proxy.process.http.total_parent_proxy_connections
    proxy.process.http.avg_transactions_per_client_connection
    proxy.process.http.avg_transactions_per_server_connection
    proxy.process.http.avg_transactions_per_parent_connection
    proxy.process.http.client_connection_time
    proxy.process.http.parent_proxy_connection_time
    proxy.process.http.server_connection_time
    proxy.process.http.cache_connection_time
    proxy.process.http.transaction_counts.errors.pre_accept_hangups
    proxy.process.http.transaction_totaltime.errors.pre_accept_hangups
    proxy.process.http.transaction_counts.errors.empty_hangups
    proxy.process.http.transaction_totaltime.errors.empty_hangups
    proxy.process.http.transaction_counts.errors.early_hangups
    proxy.process.http.transaction_totaltime.errors.early_hangups
    proxy.process.http.incoming_requests
    proxy.process.http.outgoing_requests
    proxy.process.http.incoming_responses
    proxy.process.http.invalid_client_requests
    proxy.process.http.missing_host_hdr
    proxy.process.http.get_requests
    proxy.process.http.head_requests
    proxy.process.http.trace_requests
    proxy.process.http.options_requests
    proxy.process.http.post_requests
    proxy.process.http.put_requests
    proxy.process.http.push_requests
    proxy.process.http.delete_requests
    proxy.process.http.purge_requests
    proxy.process.http.connect_requests
    proxy.process.http.extension_method_requests
    proxy.process.http.client_no_cache_requests
    proxy.process.http.broken_server_connections
    proxy.process.http.cache_lookups
    proxy.process.http.cache_writes
    proxy.process.http.cache_updates
    proxy.process.http.cache_deletes
    proxy.process.http.tunnels
    proxy.process.http.throttled_proxy_only
    proxy.process.http.request_taxonomy.i0_n0_m0
    proxy.process.http.request_taxonomy.i1_n0_m0
    proxy.process.http.request_taxonomy.i0_n1_m0
    proxy.process.http.request_taxonomy.i1_n1_m0
    proxy.process.http.request_taxonomy.i0_n0_m1
    proxy.process.http.request_taxonomy.i1_n0_m1
    proxy.process.http.request_taxonomy.i0_n1_m1
    proxy.process.http.request_taxonomy.i1_n1_m1
    proxy.process.http.icp_suggested_lookups
    proxy.process.http.client_transaction_time
    proxy.process.http.client_write_time
    proxy.process.http.server_read_time
    proxy.process.http.icp_transaction_time
    proxy.process.http.icp_raw_transaction_time
    proxy.process.http.parent_proxy_transaction_time
    proxy.process.http.parent_proxy_raw_transaction_time
    proxy.process.http.server_transaction_time
    proxy.process.http.server_raw_transaction_time
    proxy.process.http.user_agent_request_header_total_size
    proxy.process.http.user_agent_response_header_total_size
    proxy.process.http.user_agent_request_document_total_size
    proxy.process.http.user_agent_response_document_total_size
    proxy.process.http.origin_server_request_header_total_size
    proxy.process.http.origin_server_response_header_total_size
    proxy.process.http.origin_server_request_document_total_size
    proxy.process.http.origin_server_response_document_total_size
    proxy.process.http.parent_proxy_request_total_bytes
    proxy.process.http.parent_proxy_response_total_bytes
    proxy.process.http.pushed_response_header_total_size
    proxy.process.http.pushed_document_total_size
    proxy.process.http.response_document_size_100
    proxy.process.http.response_document_size_1K
    proxy.process.http.response_document_size_3K
    proxy.process.http.response_document_size_5K
    proxy.process.http.response_document_size_10K
    proxy.process.http.response_document_size_1M
    proxy.process.http.response_document_size_inf
    proxy.process.http.request_document_size_100
    proxy.process.http.request_document_size_1K
    proxy.process.http.request_document_size_3K
    proxy.process.http.request_document_size_5K
    proxy.process.http.request_document_size_10K
    proxy.process.http.request_document_size_1M
    proxy.process.http.request_document_size_inf
    proxy.process.http.user_agent_speed_bytes_per_sec_100
    proxy.process.http.user_agent_speed_bytes_per_sec_1K
    proxy.process.http.user_agent_speed_bytes_per_sec_10K
    proxy.process.http.user_agent_speed_bytes_per_sec_100K
    proxy.process.http.user_agent_speed_bytes_per_sec_1M
    proxy.process.http.user_agent_speed_bytes_per_sec_10M
    proxy.process.http.user_agent_speed_bytes_per_sec_100M
    proxy.process.http.origin_server_speed_bytes_per_sec_100
    proxy.process.http.origin_server_speed_bytes_per_sec_1K
    proxy.process.http.origin_server_speed_bytes_per_sec_10K
    proxy.process.http.origin_server_speed_bytes_per_sec_100K
    proxy.process.http.origin_server_speed_bytes_per_sec_1M
    proxy.process.http.origin_server_speed_bytes_per_sec_10M
    proxy.process.http.origin_server_speed_bytes_per_sec_100M
    proxy.process.http.total_transactions_time
    proxy.process.http.total_transactions_think_time
    proxy.process.http.cache_hit_fresh
    proxy.process.http.cache_hit_revalidated
    proxy.process.http.cache_hit_ims
    proxy.process.http.cache_hit_stale_served
    proxy.process.http.cache_miss_cold
    proxy.process.http.cache_miss_changed
    proxy.process.http.cache_miss_client_no_cache
    proxy.process.http.cache_miss_client_not_cacheable
    proxy.process.http.cache_miss_ims
    proxy.process.http.cache_read_error
    proxy.process.http.tcp_hit_count_stat
    proxy.process.http.tcp_hit_user_agent_bytes_stat
    proxy.process.http.tcp_hit_origin_server_bytes_stat
    proxy.process.http.tcp_miss_count_stat
    proxy.process.http.tcp_miss_user_agent_bytes_stat
    proxy.process.http.tcp_miss_origin_server_bytes_stat
    proxy.process.http.tcp_expired_miss_count_stat
    proxy.process.http.tcp_expired_miss_user_agent_bytes_stat
    proxy.process.http.tcp_expired_miss_origin_server_bytes_stat
    proxy.process.http.tcp_refresh_hit_count_stat
    proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat
    proxy.process.http.tcp_refresh_hit_origin_server_bytes_stat
    proxy.process.http.tcp_refresh_miss_count_stat
    proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat
    proxy.process.http.tcp_refresh_miss_origin_server_bytes_stat
    proxy.process.http.tcp_client_refresh_count_stat
    proxy.process.http.tcp_client_refresh_user_agent_bytes_stat
    proxy.process.http.tcp_client_refresh_origin_server_bytes_stat
    proxy.process.http.tcp_ims_hit_count_stat
    proxy.process.http.tcp_ims_hit_user_agent_bytes_stat
    proxy.process.http.tcp_ims_hit_origin_server_bytes_stat
    proxy.process.http.tcp_ims_miss_count_stat
    proxy.process.http.tcp_ims_miss_user_agent_bytes_stat
    proxy.process.http.tcp_ims_miss_origin_server_bytes_stat
    proxy.process.http.err_client_abort_count_stat
    proxy.process.http.err_client_abort_user_agent_bytes_stat
    proxy.process.http.err_client_abort_origin_server_bytes_stat
    proxy.process.http.err_connect_fail_count_stat
    proxy.process.http.err_connect_fail_user_agent_bytes_stat
    proxy.process.http.err_connect_fail_origin_server_bytes_stat
    proxy.process.http.misc_count_stat
    proxy.process.http.misc_user_agent_bytes_stat
    proxy.process.http.background_fill_bytes_aborted_stat
    proxy.process.http.background_fill_bytes_completed_stat
    proxy.process.http.cache_write_errors
    proxy.process.http.cache_read_errors
    proxy.process.http.100_responses
    proxy.process.http.101_responses
    proxy.process.http.1xx_responses
    proxy.process.http.200_responses
    proxy.process.http.201_responses
    proxy.process.http.202_responses
    proxy.process.http.203_responses
    proxy.process.http.204_responses
    proxy.process.http.205_responses
    proxy.process.http.206_responses
    proxy.process.http.2xx_responses
    proxy.process.http.300_responses
    proxy.process.http.301_responses
    proxy.process.http.302_responses
    proxy.process.http.303_responses
    proxy.process.http.304_responses
    proxy.process.http.305_responses
    proxy.process.http.307_responses
    proxy.process.http.3xx_responses
    proxy.process.http.400_responses
    proxy.process.http.401_responses
    proxy.process.http.402_responses
    proxy.process.http.403_responses
    proxy.process.http.404_responses
    proxy.process.http.405_responses
    proxy.process.http.406_responses
    proxy.process.http.407_responses
    proxy.process.http.408_responses
    proxy.process.http.409_responses
    proxy.process.http.410_responses
    proxy.process.http.411_responses
    proxy.process.http.412_responses
    proxy.process.http.413_responses
    proxy.process.http.414_responses
    proxy.process.http.415_responses
    proxy.process.http.416_responses
    proxy.process.http.4xx_responses
    proxy.process.http.500_responses
    proxy.process.http.501_responses
    proxy.process.http.502_responses
    proxy.process.http.503_responses
    proxy.process.http.504_responses
    proxy.process.http.505_responses
    proxy.process.http.5xx_responses
    proxy.process.http.transaction_counts.hit_fresh
    proxy.process.http.transaction_totaltime.hit_fresh
    proxy.process.http.transaction_counts.hit_fresh.process
    proxy.process.http.transaction_totaltime.hit_fresh.process
    proxy.process.http.transaction_counts.hit_revalidated
    proxy.process.http.transaction_totaltime.hit_revalidated
    proxy.process.http.transaction_counts.miss_cold
    proxy.process.http.transaction_totaltime.miss_cold
    proxy.process.http.transaction_counts.miss_not_cacheable
    proxy.process.http.transaction_totaltime.miss_not_cacheable
    proxy.process.http.transaction_counts.miss_changed
    proxy.process.http.transaction_totaltime.miss_changed
    proxy.process.http.transaction_counts.miss_client_no_cache
    proxy.process.http.transaction_totaltime.miss_client_no_cache
    proxy.process.http.transaction_counts.errors.aborts
    proxy.process.http.transaction_totaltime.errors.aborts
    proxy.process.http.transaction_counts.errors.possible_aborts
    proxy.process.http.transaction_totaltime.errors.possible_aborts
    proxy.process.http.transaction_counts.errors.connect_failed
    proxy.process.http.transaction_totaltime.errors.connect_failed
    proxy.process.http.transaction_counts.errors.other
    proxy.process.http.transaction_totaltime.errors.other
    proxy.process.http.transaction_counts.other.unclassified
    proxy.process.http.transaction_totaltime.other.unclassified
    proxy.process.http.total_x_redirect_count
    proxy.process.icp.config_mgmt_callouts
    proxy.process.icp.reconfig_polls
    proxy.process.icp.reconfig_events
    proxy.process.icp.invalid_poll_data
    proxy.process.icp.no_data_read
    proxy.process.icp.short_read
    proxy.process.icp.invalid_sender
    proxy.process.icp.read_not_v2_icp
    proxy.process.icp.icp_remote_query_requests
    proxy.process.icp.icp_remote_responses
    proxy.process.icp.cache_lookup_success
    proxy.process.icp.cache_lookup_fail
    proxy.process.icp.query_response_write
    proxy.process.icp.query_response_partial_write
    proxy.process.icp.no_icp_request_for_response
    proxy.process.icp.icp_response_request_nolock
    proxy.process.icp.icp_start_icpoff
    proxy.process.icp.send_query_partial_write
    proxy.process.icp.icp_queries_no_expected_replies
    proxy.process.icp.icp_query_hits
    proxy.process.icp.icp_query_misses
    proxy.process.icp.invalid_icp_query_response
    proxy.process.icp.icp_query_requests
    proxy.process.icp.total_icp_response_time
    proxy.process.icp.total_udp_send_queries
    proxy.process.icp.total_icp_request_time
    proxy.process.net.connections_currently_open
    proxy.process.net.accepts_currently_open
    proxy.process.socks.connections_currently_open
    proxy.process.cache.bytes_used
    proxy.process.cache.bytes_total
    proxy.process.cache.ram_cache.bytes_used
    proxy.process.cache.ram_cache.hits
    proxy.process.cache.pread_count
    proxy.process.cache.percent_full
    proxy.process.cache.lookup.active
    proxy.process.cache.lookup.success
    proxy.process.cache.lookup.failure
    proxy.process.cache.read.active
    proxy.process.cache.read.success
    proxy.process.cache.read.failure
    proxy.process.cache.write.active
    proxy.process.cache.write.success
    proxy.process.cache.write.failure
    proxy.process.cache.write.backlog.failure
    proxy.process.cache.update.active
    proxy.process.cache.update.success
    proxy.process.cache.update.failure
    proxy.process.cache.remove.active
    proxy.process.cache.remove.success
    proxy.process.cache.remove.failure
    proxy.process.cache.evacuate.active
    proxy.process.cache.evacuate.success
    proxy.process.cache.evacuate.failure
    proxy.process.cache.scan.active
    proxy.process.cache.scan.success
    proxy.process.cache.scan.failure
    proxy.process.cache.direntries.total
    proxy.process.cache.direntries.used
    proxy.process.cache.directory_collision
    proxy.process.cache.frags_per_doc.1
    proxy.process.cache.frags_per_doc.2
    proxy.process.cache.frags_per_doc.3+
    proxy.process.cache.read_busy.success
    proxy.process.cache.read_busy.failure
    proxy.process.cache.write_bytes_stat
    proxy.process.cache.vector_marshals
    proxy.process.cache.hdr_marshals
    proxy.process.cache.hdr_marshal_bytes
    proxy.process.cache.gc_bytes_evacuated
    proxy.process.cache.gc_frags_evacuated
    proxy.process.hostdb.total_hits
    proxy.process.dns.success_avg_time
    proxy.process.dns.in_flight
    proxy.process.congestion.congested_on_conn_failures
    proxy.process.congestion.congested_on_max_connection
    proxy.process.cluster.connections_open
    proxy.process.cluster.connections_opened
    proxy.process.cluster.connections_closed
    proxy.process.cluster.slow_ctrl_msgs_sent
    proxy.process.cluster.connections_locked
    proxy.process.cluster.reads
    proxy.process.cluster.read_bytes
    proxy.process.cluster.writes
    proxy.process.cluster.write_bytes
    proxy.process.cluster.control_messages_sent
    proxy.process.cluster.control_messages_received
    proxy.process.cluster.op_delayed_for_lock
    proxy.process.cluster.connections_bumped
    proxy.process.cluster.net_backup
    proxy.process.cluster.nodes
    proxy.process.cluster.machines_allocated
    proxy.process.cluster.machines_freed
    proxy.process.cluster.configuration_changes
    proxy.process.cluster.delayed_reads
    proxy.process.cluster.byte_bank_used
    proxy.process.cluster.alloc_data_news
    proxy.process.cluster.write_bb_mallocs
    proxy.process.cluster.partial_reads
    proxy.process.cluster.partial_writes
    proxy.process.cluster.cache_outstanding
    proxy.process.cluster.remote_op_timeouts
    proxy.process.cluster.remote_op_reply_timeouts
    proxy.process.cluster.chan_inuse
    proxy.process.cluster.open_delays
    proxy.process.cluster.connections_avg_time
    proxy.process.cluster.control_messages_avg_send_time
    proxy.process.cluster.control_messages_avg_receive_time
    proxy.process.cluster.open_delay_time
    proxy.process.cluster.cache_callback_time
    proxy.process.cluster.rmt_cache_callback_time
    proxy.process.cluster.lkrmt_cache_callback_time
    proxy.process.cluster.local_connection_time
    proxy.process.cluster.remote_connection_time
    proxy.process.cluster.rdmsg_assemble_time
    proxy.process.cluster.cluster_ping_time
    proxy.process.cluster.cache_callbacks
    proxy.process.cluster.rmt_cache_callbacks
    proxy.process.cluster.lkrmt_cache_callbacks
    proxy.process.cluster.local_connections_closed
    proxy.process.cluster.remote_connections_closed
    proxy.process.cluster.setdata_no_clustervc
    proxy.process.cluster.setdata_no_tunnel
    proxy.process.cluster.setdata_no_cachevc
    proxy.process.cluster.setdata_no_cluster
    proxy.process.cluster.vc_write_stall
    proxy.process.cluster.no_remote_space
    proxy.process.cluster.level1_bank
    proxy.process.cluster.multilevel_bank
    proxy.process.cluster.vc_cache_insert_lock_misses
    proxy.process.cluster.vc_cache_inserts
    proxy.process.cluster.vc_cache_lookup_lock_misses
    proxy.process.cluster.vc_cache_lookup_hits
    proxy.process.cluster.vc_cache_lookup_misses
    proxy.process.cluster.vc_cache_scans
    proxy.process.cluster.vc_cache_scan_lock_misses
    proxy.process.cluster.vc_cache_purges
    proxy.process.cluster.write_lock_misses
    proxy.process.log.log_files_open
    proxy.process.log.log_files_space_used
    proxy.process.http.background_fill_current_count
    proxy.process.http.current_client_connections
    proxy.process.http.current_active_client_connections
    proxy.process.http.current_client_transactions
    proxy.process.http.current_parent_proxy_transactions
    proxy.process.http.current_icp_transactions
    proxy.process.http.current_server_transactions
    proxy.process.http.current_parent_proxy_raw_transactions
    proxy.process.http.current_icp_raw_transactions
    proxy.process.http.current_server_raw_transactions
    proxy.process.http.current_parent_proxy_connections
    proxy.process.http.current_server_connections
    proxy.process.http.current_cache_connections
    proxy.process.update.successes
    proxy.process.update.no_actions
    proxy.process.update.fails
    proxy.process.update.unknown_status
    proxy.process.update.state_machines
    proxy.process.cache.volume_0.bytes_used
    proxy.process.cache.volume_0.bytes_total
    proxy.process.cache.volume_0.ram_cache.bytes_used
    proxy.process.cache.volume_0.ram_cache.hits
    proxy.process.cache.volume_0.pread_count
    proxy.process.cache.volume_0.percent_full
    proxy.process.cache.volume_0.lookup.active
    proxy.process.cache.volume_0.lookup.success
    proxy.process.cache.volume_0.lookup.failure
    proxy.process.cache.volume_0.read.active
    proxy.process.cache.volume_0.read.success
    proxy.process.cache.volume_0.read.failure
    proxy.process.cache.volume_0.write.active
    proxy.process.cache.volume_0.write.success
    proxy.process.cache.volume_0.write.failure
    proxy.process.cache.volume_0.write.backlog.failure
    proxy.process.cache.volume_0.update.active
    proxy.process.cache.volume_0.update.success
    proxy.process.cache.volume_0.update.failure
    proxy.process.cache.volume_0.remove.active
    proxy.process.cache.volume_0.remove.success
    proxy.process.cache.volume_0.remove.failure
    proxy.process.cache.volume_0.evacuate.active
    proxy.process.cache.volume_0.evacuate.success
    proxy.process.cache.volume_0.evacuate.failure
    proxy.process.cache.volume_0.scan.active
    proxy.process.cache.volume_0.scan.success
    proxy.process.cache.volume_0.scan.failure
    proxy.process.cache.volume_0.direntries.total
    proxy.process.cache.volume_0.direntries.used
    proxy.process.cache.volume_0.directory_collision
    proxy.process.cache.volume_0.frags_per_doc.1
    proxy.process.cache.volume_0.frags_per_doc.2
    proxy.process.cache.volume_0.frags_per_doc.3+
    proxy.process.cache.volume_0.read_busy.success
    proxy.process.cache.volume_0.read_busy.failure
    proxy.process.cache.volume_0.write_bytes_stat
    proxy.process.cache.volume_0.vector_marshals
    proxy.process.cache.volume_0.hdr_marshals
    proxy.process.cache.volume_0.hdr_marshal_bytes
    proxy.process.cache.volume_0.gc_bytes_evacuated
    proxy.process.cache.volume_0.gc_frags_evacuated

Examples
========

Configure Traffic Server to log in Squid format::

    $ traffic_line -s proxy.config.log.squid_log_enabled -v 1
    $ traffic_line -s proxy.config.log.squid_log_is_ascii -v 1
    $ traffic_line -x

Files
=====

:file:`records.config`, :file:`ssl_multicert.config`

See also
========

:manpage:`records.config(5)`
