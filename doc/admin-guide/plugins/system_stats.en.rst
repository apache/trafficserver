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

.. include:: ../../common.defs

.. _admin-plugins-system_stats:

System Stats Plugin
********************

Purpose
=======

This plugin inserts system statistics in to the stats list that can then be
retrieved either with stats_over_http or any other method using the management port.
These can be used to diagnose issues like excessive load or networking performance.

Installation
============

To enable this plugin, build with experimental plugin support add to the :file:`plugin.config` file::

    system_stats.so

Caveats
=======

This plugin depends greatly on the sysfs interface so it is mostly useful only for Linux hosts, however
it will build for other hosts.

Examples
========

Some example output is:

.. code::

	"proxy.process.ssl.cipher.user_agent.DES-CBC3-SHA": 0,
	"proxy.process.ssl.cipher.user_agent.PSK-3DES-EDE-CBC-SHA": 0,
	"plugin.system_stats.loadavg.one": 136128,
	"plugin.system_stats.loadavg.five": 132032,
	"plugin.system_stats.loadavg.ten": 88864,
	"plugin.system_stats.current_processes": 503,
	"plugin.system_stats.net.enp0s3.speed": 1000,
	"plugin.system_stats.net.enp0s3.collisions": 0,
	"plugin.system_stats.net.enp0s3.multicast": 0,
	"plugin.system_stats.net.enp0s3.rx_bytes": 6783959,
	"plugin.system_stats.net.enp0s3.rx_compressed": 0,
	"plugin.system_stats.net.enp0s3.rx_crc_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_dropped": 0,
	"plugin.system_stats.net.enp0s3.rx_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_fifo_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_frame_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_length_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_missed_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_nohandler": 0,
	"plugin.system_stats.net.enp0s3.rx_over_errors": 0,
	"plugin.system_stats.net.enp0s3.rx_packets": 9119,
	"plugin.system_stats.net.enp0s3.tx_aborted_errors": 0,
	"plugin.system_stats.net.enp0s3.tx_bytes": 922054,
	"plugin.system_stats.net.enp0s3.tx_carrier_errors": 0,
	"plugin.system_stats.net.enp0s3.tx_compressed": 0,
	"plugin.system_stats.net.enp0s3.tx_dropped": 0,
	"plugin.system_stats.net.enp0s3.tx_errors": 0,
	"plugin.system_stats.net.enp0s3.tx_fifo_errors": 0,
	"plugin.system_stats.net.enp0s3.tx_heartbeat_errors": 0,
	"plugin.system_stats.net.enp0s3.tx_packets": 6013,
	"plugin.system_stats.net.enp0s3.tx_window_errors": 0,
	"proxy.process.cache.volume_0.bytes_used": 0,
	"proxy.process.cache.volume_0.bytes_total": 268066816,

This shows the system statistics inserted in with other stats. The above output
displays the current load average, number of processes, and information on any network interfaces.

This data is only updated every five seconds to not create large overhead.
