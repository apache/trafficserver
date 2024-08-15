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


New or modified Configurations
------------------------------

Combined Connect Timeouts
^^^^^^^^^^^^^^^^^^^^^^^^^

The configuration settings :ts:cv: `proxy.config.http.parent_proxy.connect_attempts_timeout` and :ts:cv: `proxy.config.http.post_connect_attempts_timeout` have been removed.
All connect timeouts are controlled by :ts:cv: `proxy.config.http.connect_attempts_timeout`.




Logging and Metrics
-------------------

Plugins
-------

* authproxy - ``--forward-header-prefix`` parameter has been added
* prefetch - Cmcd-Request header support has been added
* xdebug - ``--enable`` option to selectively enable features has been added
* system_stats - Stats about memory have been added

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
