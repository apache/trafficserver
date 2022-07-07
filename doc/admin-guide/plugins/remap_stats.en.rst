.. include:: ../../common.defs

.. _admin-plugins-remap-stats:

Remap Stats Plugin
******************

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


This global plugin adds remap stats to the Traffic Server statistics.

Installation
============

Add the following line to :file:`plugin.config`::

    remap_stats.so

In this case, the plugin will use the default behaviour:

- Create stats for all remap stats
- URLs will be the pristine URL from the client
- Stats will not be persistent

Options
=======

Flags and options are:

.. option:: --post-remap-host, -P

   Whether to use the post-remap host in the URL, instead of the pristine client URL.

.. option:: --persistent, -p

   Whether to use persistent stats.

Options in the code:

.. c:macro:: MAX_STAT_LENGTH

   The maximum length of any stat name. Since stat names include the remap FQDN, this affects the maximum FQDN length that can be included in the stat.

Stats
=====

The following stats are added to the Traffic Server statistics, for every remap rule::

    plugin.remap_stats.<fqdn>.in_bytes
    plugin.remap_stats.<fqdn>.out_bytes
    plugin.remap_stats.<fqdn>.status_2xx
    plugin.remap_stats.<fqdn>.status_3xx
    plugin.remap_stats.<fqdn>.status_4xx
    plugin.remap_stats.<fqdn>.status_5xx
    plugin.remap_stats.<fqdn>.status_unknown

The ``<fqdn>`` will be either the pristine client URL or the remapped URL, depending on whether ``--post-remap-host`` is set.

All stat values are integers.

These stats are available anywhere Traffic Server statistics can be viewed, such as the ``stats_over_http`` plugin and ``traffic_ctl metric get``. See :ref:`admin-stats-accessing`.
