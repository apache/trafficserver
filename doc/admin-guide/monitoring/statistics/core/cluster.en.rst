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

.. include:: ../../../../common.defs

.. _admin-stats-core-cluster:

Cluster
*******

The statistics documented in this section relate to |TS| clusters, which are
synchronized sets of |TS| instances running across multiple hosts. For more
information on configuring and using the clustering feature, please refer to
the :ref:`traffic-server-cluster` chapter of the :ref:`admin-guide`.

.. ts:stat:: global proxy.node.cluster.nodes integer
   :type: gauge

   Represents the number of |TS| instances which are members of the same cluster
   as this node.

.. ts:stat:: global proxy.process.cluster.alloc_data_news integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.byte_bank_used integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.cache_callbacks integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.cache_callback_time float
.. ts:stat:: global proxy.process.cluster.cache_outstanding integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.chan_inuse integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.cluster_ping_time float
   :type: gauge

.. ts:stat:: global proxy.process.cluster.configuration_changes integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.connections_avg_time float
   :type: gauge

.. ts:stat:: global proxy.process.cluster.connections_bumped integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.connections_closed integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.connections_opened integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.connections_open integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.connections_read_locked integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.connections_write_locked integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.control_messages_avg_receive_time float
.. ts:stat:: global proxy.process.cluster.control_messages_avg_send_time float
.. ts:stat:: global proxy.process.cluster.control_messages_received integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.control_messages_sent integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.delayed_reads integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.level1_bank integer
.. ts:stat:: global proxy.process.cluster.lkrmt_cache_callbacks integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.lkrmt_cache_callback_time float
.. ts:stat:: global proxy.process.cluster.local_connections_closed integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.local_connection_time float
.. ts:stat:: global proxy.process.cluster.machines_allocated integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.machines_freed integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.multilevel_bank integer
.. ts:stat:: global proxy.process.cluster.net_backup integer
.. ts:stat:: global proxy.process.cluster.nodes integer
   :type: gauge

.. ts:stat:: global proxy.process.cluster.no_remote_space integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.op_delayed_for_lock integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.open_delays integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.open_delay_time float
.. ts:stat:: global proxy.process.cluster.partial_reads integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.partial_writes integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.rdmsg_assemble_time float
.. ts:stat:: global proxy.process.cluster.read_bytes integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.cluster.reads integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.remote_connections_closed integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.remote_connection_time float
.. ts:stat:: global proxy.process.cluster.remote_op_reply_timeouts integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.remote_op_timeouts integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.rmt_cache_callbacks integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.rmt_cache_callback_time float
.. ts:stat:: global proxy.process.cluster.setdata_no_cachevc integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.setdata_no_cluster integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.setdata_no_clustervc integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.setdata_no_tunnel integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.slow_ctrl_msgs_sent integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_insert_lock_misses integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_inserts integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_lookup_hits integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_lookup_lock_misses integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_lookup_misses integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_purges integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_scan_lock_misses integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_cache_scans integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_read_list_len integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_write_list_len integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.vc_write_stall integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.write_bb_mallocs integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.write_bytes integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.cluster.write_lock_misses integer
   :type: counter

.. ts:stat:: global proxy.process.cluster.writes integer
   :type: counter


