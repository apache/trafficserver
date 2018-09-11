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

.. _developer-introduction:

Introduction
************

This guide has the following basic components:

-  Introduction and overview.

-  Tutorials about writing specific kinds of plugins: HTTP header-based
   plugins, content transformation plugins, and protocol plugins.

-  Guides about specific interfaces.

-  Reference material.

If you're new to writing |TS| plugins, :ref:`developer-plugins-getting-started`
should be your starting point. :ref:`developer-plugins-header-based-examples`
provides details about plugins that work on HTTP headers, while
:ref:`developer-plugins-http-transformations` explains how to write a plugin
that transforms or scans the body of an HTTP response.
:ref:`developer-plugins-new-protocol-plugins` provides essential information if
you want to support your own protocol on |TS|.

For a reference to the C API functions and types that your plugin will use,
refer to the :ref:`developer-api-reference`.

Below is a section-by-section breakdown of this guide:

:ref:`developer-header-files` 
   The header file directory structure, where to find headers for particular tasks.

:ref:`developer-plugins-getting-started`
   How to compile and load plugins. Walks through a simple "hello world"
   example; explains how to initialize and register plugins. Basic structures
   that all plugins use: events, continuations, and how to hook on to |TS|
   processes. Detailed explication of a sample blacklisting plugin.

:ref:`developer-plugins-examples-query-remap`
   This chapter demonstrates on a practical example how you can
   exploit the Traffic Server remap API for your plugins.

:ref:`developer-plugins-header-based-examples`
   Detailed explanation about writing plugins that work on HTTP
   headers; discusses sample blacklisting and basic authorization
   plugins.

:ref:`developer-plugins-http-transformations`
   Detailed explanation of the null_transform example; also discusses
   ``VConnections``, ``VIOs``, and IO buffers.

:ref:`developer-plugins-new-protocol-plugins`
   Detailed explanation of a sample protocol plugin that supports a
   synthetic protocol. Discusses ``VConnections`` and mutexes, as well
   as the new ``NetConnection``, DNS lookup, logging, and cache APIs.

The remaining sections comprise the API function reference and are organized by
function type:

:ref:`developer-plugins-interfaces`
   Details error-writing and tracing functions, thread functions, and |TS| API
   versions of the ``malloc`` and ``fopen`` families. The |TS| API versions
   overcome various C library limitations.

:ref:`developer-plugins-hooks-and-transactions`
   Functions in this chapter hook your plugin to Traffic Server HTTP processes.

:ref:`developer-plugins-http-headers`
   Contains instructions for implementing performance enhancements for
   all plugins that manipulate HTTP headers. These functions examine and
   modify HTTP headers, MIME headers, URLs, and the marshal buffers that
   contain header information. If you are working with headers, then be
   sure to read this chapter.

:ref:`developer-plugins-mutexes`

:ref:`developer-plugins-continuations`
   Continuations provide the basic callback mechanism and data
   abstractions used in Traffic Server.

:ref:`developer-plugins-configuration`

:ref:`developer-plugins-actions`
   Describes how to use ``TSActions`` and the ``TSDNSLookup`` API.

:ref:`developer-plugins-io`
   Describes how to use the Traffic Server IO interfaces:
   ``TSVConnection``, ``TSVIO``, ``TSIOBuffer``, ``TSNetVConnection``,
   the Cache API.

:ref:`developer-plugins-management`
   These functions enable you to set up a configuration interface for
   plugins, access installed plugin files, and set up plugin licensing.

:ref:`developer-plugins-add-statistics`
   These functions add statistics to your plugin.

:ref:`developer-api-ref-functions`
   Traffic Server API Function Documentation.

.. toctree::
   :hidden:

   header-file-structure.en
