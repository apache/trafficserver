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

What's New in ATS v10.x
=======================

This version of |ATS| includes over <x> commits, from <y> pull requests. A
total of <z> contributors have participated in this development cycle.

.. toctree::
   :maxdepth: 1

New Features
------------

* JSON-RPC based interface for administrative API

   |TS| now exposes a JSON-RPC node to interact with external tools. Check :ref:`developer-guide-jsonrpc` for more details.

* traffic_ctl has a new command ``monitor`` to show a continuously updating list of metrics

* :file:`ip_allow.yaml` and :file:`remap.config` now support named IP ranges via IP
  Categories. See the ``ip_categories`` key definition in :file:`ip_allow.yaml`
  for information about their use and definitions.

* :file:`sni.yaml` ``fqdn:tunnel_route``, beside the already supported match group
  number, configuration now also supports the destination port using a variable specification
  either for the incoming connection port or the port that was specified by the
  incoming Proxy Protocol payload. Check :file:`sni.yaml` for more information.

* The records.yaml entry :ts:cv:`proxy.config.system_clock` was added to control the underlying
  system clock that ATS uses for internal timing

* OCSP requests is now be able to use GET method. See ts:cv`proxy.config.ssl.ocsp.request_mode` for more information.

* TSHttpSsnInfoIntGet has been added.

New or modified Configurations
------------------------------

ip_allow.yaml and remap.config ACL actions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There are two new sets of actions for HTTP request method filtering introduced in |TS| 10.x:

- Both :file:`ip_allow.yaml` and :file:`remap.config` now support the ``set_allow`` and ``set_deny`` actions. These
  actions both behave like ``allow`` and ``deny`` did for :file:`ip_allow.yaml` pre |TS| 10.x.
- In addition, :file:`remap.config` now supports ``add_allow`` and ``add_deny`` actions. These behave like ``allow``
  and ``deny`` actions did for :file:`remap.config` ACLs pre |TS| 10.x.

The details about the motivation and behavior of these actions are documented in :ref:`acl-filters`.

Logging and Metrics
-------------------

The numbers of HTTP/2 frames received have been added as metrics.

Plugins
-------

* authproxy - ``--forward-header-prefix`` parameter has been added
* prefetch - Cmcd-Request header support has been added
* xdebug - ``--enable`` option to selectively enable features has been added
* system_stats - Stats about memory have been added
* slice plugin - This plugin was promoted to stable.

JSON-RPC
^^^^^^^^

   Remote clients, like :ref:`traffic_ctl_jsonrpc` have now bi-directional access to the plugin space. For more details check :ref:`jsonrpc_development`.

Replaced autotools build system with cmake
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

See :ref:`admin-installing` for more information

Switch to C++20
^^^^^^^^^^^^^^^

Plugins are now required to be compiled as C++ code, rather than straight C.
The API is tested with C++20, so code compatible with this version is preferred.
``TSDebug`` and related functions are removed.  Debug tracing should now be done
using cpp:func:`Dbg` and related functions, as in |TS| core code.

C++ Plugin API Deprecated
^^^^^^^^^^^^^^^^^^^^^^^^^

It is deprecated in this release.  It will be deleted in ATS 11.

Symbols With INKUDP Prefix
^^^^^^^^^^^^^^^^^^^^^^^^^^

In the plugin API, all types and functions starting with the prefix INKUDP are removed.

New plugin hook for request sink transformation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A new hook, ``TS_HTTP_REQUEST_CLIENT_HOOK``, has been added. This provides the analoguas functionality of
``TS_HTTP_RESPONSE_CLIENT_HOOK``, for request bodies.

HTTP/2
^^^^^^

* Support for HTTP/2 on origin server connections has been added. This is disabled by default. For more details check :ts:cv:`proxy.config.ssl.client.alpn_protocols`
* Support for CONNECT method has been added.
* Window size control has been improved. For more details check :ts:cv:`proxy.config.http2.flow_control.policy_in`

HTTP UI Removed
^^^^^^^^^^^^^^^

The stats and cache inspector pages were unmaintained and removed in this
release.


