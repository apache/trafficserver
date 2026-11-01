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

.. important::

   This plugin exposes the shared |TS| cache with no authentication or
   access control. Read `Security`_ before deploying it, and in particular
   before binding it to any non-loopback address.

Installation
============

Add the following line to :file:`plugin.config`::

    tsmemcache.so 11211

In this case, the plugin will use the default behaviour:

-  Listen for memcache traffic on the default memcache port 11211
-  Bind only to the loopback address (``127.0.0.1``), so the listener is
   not reachable from other hosts on the network
-  Write memcache data to the cache in the background (without waiting)

To bind the listener to a specific address, pass it as a second argument::

    tsmemcache.so 11211 127.0.0.1

See `Security`_ before binding to any non-loopback address.

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

``bind_ip``: Optional. The local IP address to bind the listener to.
Defaults to ``127.0.0.1`` (loopback only). Specifying a non-loopback
address (for example ``0.0.0.0`` or a specific interface address)
deliberately exposes the listener to the network; see `Security`_.

Options in the code:
``TSMEMCACHE_WRITE_SYNC`` whether or not to wait for the write to complete.

Security
========

.. important::

   The ``tsmemcache`` plugin operates directly on the shared |TS| object
   cache: any client that can reach the listener can read, overwrite, and
   delete cache entries, including content served by the HTTP proxy. The
   plugin performs **no authentication** (the binary protocol's SASL
   commands are not implemented) and applies **no**
   :file:`ip_allow.yaml` ACL on its accept path -- those controls exist
   only on the HTTP/2/3 session-accept paths, which this plugin does not
   use.

   For this reason the listener binds to the loopback address
   (``127.0.0.1``) by default, so it is not reachable from the network.
   Only bind to a non-loopback address if the port is otherwise protected
   (for example by a host or network firewall that restricts access to
   trusted clients). When a non-loopback address is configured the plugin
   logs a warning at startup, and an invalid bind address causes the
   plugin to fail to load rather than silently fall back to a wildcard
   bind.
