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

* :file:`ip_allow.yaml` and :file:`remap.config` now support named IP ranges via IP
  Categories. See the ``ip_categories`` key definition in :file:`ip_allow.yaml`
  for information about their use and definitions.


New or modified Configurations
------------------------------

Combined Connect Timeouts
^^^^^^^^^^^^^^^^^^^^^^^^^

The configuration settings :ts:cv: `proxy.config.http.parent_proxy.connect_attempts_timeout` and :ts:cv: `proxy.config.http.post_connect_attempts_timeout` have been removed.
All connect timeouts are controlled by :ts:cv: `proxy.config.http.connect_attempts_timeout`.


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

Plugins
-------

* authproxy - ``--forward-header-prefix`` parameter has been added

JSON-RPC
^^^^^^^^

   Remote clients, like :ref:`traffic_ctl_jsonrpc` have now bi-directional access to the plugin space. For more details check :ref:`jsonrpc_development`.

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
