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

.. include:: ../common.defs

.. _whats_new:

What's New in ATS v9.x
=======================

This version of ATS includes over <x> commits, from <y> pull requests. A total of <z> contributors
have participated in this development cycle.

.. toctree::
   :maxdepth: 1

New Features
------------

Incompatible records.config settings
------------------------------------

These are the changes that are most likely to cause problems during an upgrade. Take special care
making sure you have updated your configurations accordingly.

Connection management
~~~~~~~~~~~~~~~~~~~~~
The old settings for origin connection management included the following settings:

* `proxy.config.http.origin_max_connections`
* `proxy.config.http.origin_max_connections_queue`
* `proxy.config.http.origin_min_keep_alive_connections`

These are all gone, and replaced with the following set of configurations:

* :ts:cv:`proxy.config.http.per_server.connection.max` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.match` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.alert_delay`
* :ts:cv:`proxy.config.http.per_server.connection.queue_size`
* :ts:cv:`proxy.config.http.per_server.connection.queue_delay`
* :ts:cv:`proxy.config.http.per_server.connection.min`

Logging and Metrics
-------------------

Plugins
-------
