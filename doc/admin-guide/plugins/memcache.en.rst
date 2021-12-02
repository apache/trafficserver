.. include:: ../../common.defs

.. _admin-plugins-memcache:

Memcache Plugin
***************

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


This plugin implements the memcache protocol for cache contents.

Installation
============

Add the following line to :file:`plugin.config`::

    tsmemcache.so 11211

In this case, the plugin will use the default behaviour:

-  Listen for memcache traffic on the default memcache port 11211
-  Write memcache data to the cache in the background (without waiting)

Configuration
=============

Alternatively, a configuration can also be specified::

    tsmemcache.so <path-to-plugin>/sample.tsmemcache.config

After modifying plugin.config, restart |TS| (sudo traffic_ctl
server restart) the configuration is also re-read when a management
update is given (sudo traffic_ctl config reload)

Options
=======

Flags and options are:

``port``: A single port to listen on for memcache protocol commands.

Options in the code:
``TSMEMCACHE_WRITE_SYNC`` whether or not to wait for the write to complete.
