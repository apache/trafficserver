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

.. _admin-stats-core-general:

General
*******

.. ts:stat:: global server string 5.3.0

   Human-readable version number string for the currently running |TS| instance.

.. ts:stat:: global proxy.node.config.reconfigure_required integer
   :type: flag

.. ts:stat:: global proxy.node.config.reconfigure_time integer

.. ts:stat:: global proxy.node.config.restart_required.cop integer
   :type: flag

.. ts:stat:: global proxy.node.config.restart_required.manager integer
   :type: flag

.. ts:stat:: global proxy.node.config.restart_required.proxy integer
   :type: flag

.. ts:stat:: global proxy.node.hostname_FQ string ats-host.example.com

   Fully-qualified domain name for the host on which |TS| is running.

.. ts:stat:: global proxy.node.hostname string ats-host

   The hostname only, without domain, for the host on which |TS| is running.

.. ts:stat:: global proxy.node.num_processes integer
   :type: gauge

   The number of :program:`traffic_server` processes running on the host.

.. ts:stat:: global proxy.node.proxy_running integer
   :type: flag

   Indicates whether any form of HTTP proxying is currently enabled in the
   running instance of |TS|.

.. ts:stat:: global proxy.node.restarts.manager.start_time integer
   :type: gauge
   :unit: seconds

   Unix epoch-time value indicating the time at which the currently-running
   :program:`traffic_manager` process was started.

.. ts:stat:: global proxy.node.restarts.proxy.cache_ready_time integer
   :type: gauge
   :unit: seconds

.. ts:stat:: global proxy.node.restarts.proxy.restart_count integer
.. ts:stat:: global proxy.node.restarts.proxy.start_time integer
.. ts:stat:: global proxy.node.restarts.proxy.stop_time integer
.. ts:stat:: global proxy.node.user_agents_total_documents_served integer
.. ts:stat:: global proxy.node.user_agent_total_bytes_avg_10s float
.. ts:stat:: global proxy.node.user_agent_total_bytes integer
.. ts:stat:: global proxy.node.user_agent_xacts_per_second float
.. ts:stat:: global proxy.node.version.manager.build_date string
.. ts:stat:: global proxy.node.version.manager.build_machine string
.. ts:stat:: global proxy.node.version.manager.build_number integer
.. ts:stat:: global proxy.node.version.manager.build_person string
.. ts:stat:: global proxy.node.version.manager.build_time string
.. ts:stat:: global proxy.node.version.manager.long string
.. ts:stat:: global proxy.node.version.manager.short float
.. ts:stat:: global proxy.process.http.tunnels integer
.. ts:stat:: global proxy.process.update.fails integer
.. ts:stat:: global proxy.process.update.no_actions integer
.. ts:stat:: global proxy.process.update.state_machines integer
.. ts:stat:: global proxy.process.update.successes integer
.. ts:stat:: global proxy.process.update.unknown_status integer
.. ts:stat:: global proxy.process.version.server.build_date string Apr 20 2015

   Date on which the running instance of |TS| was compiled.

.. ts:stat:: global proxy.process.version.server.build_machine string

   The hostname of the machine on which the running instance of |TS| was
   compiled.

.. ts:stat:: global proxy.process.version.server.build_number string 042020

   The string representation of the |TS| build number for the running instance.

.. ts:stat:: global proxy.process.version.server.build_person string jdoe

   The effective username which compiled the running instance of |TS|.

.. ts:stat:: global proxy.process.version.server.build_time string 20:14:09

   The time at which the running instance of |TS| was compiled.

.. ts:stat:: global proxy.process.version.server.long string Apache Traffic Server - traffic_server - 5.3.0 - (build # 042020 on Apr 20 2015 at 20:14:09)

   A string representing the build information of the running instance of |TS|.
   Includes the software name, primary binary name, release number, build
   number, build date, and build time.

.. ts:stat:: global proxy.process.version.server.short float 5.3.0

   A shortened string containing the release number of the running instance of
   |TS|.

