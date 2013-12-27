How to Use This Book
********************

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

This book has the following basic components:

-  Introduction and overview

-  Tutorials about writing specific kinds of plugins: HTTP header-based
   plugins, content transformation plugins, and protocol plugins

-  Guides about specific interfaces

-  Reference material

If you're new to writing Traffic Server plugins, then read
:doc:`../getting-started.en` and :doc:`../how-to-create-trafficserver-plugins.en`,
and use the remaining chapters as needed. :doc:`../header-based-plugin-examples.en`
provides details about plugins that work on HTTP headers, while
:doc:`../http-transformation-plugin.en` explains how to write a plugin that
transforms or scans the body of an HTTP response. :doc:`../new-protocol-plugins.en`
provides essential information if you want to support your own protocol on
Traffic Server.

You can look up information in the following reference sections:

-  `Index <concept-index>`_: lists information by subject
-  `Function
   Index <http://ci.apache.org/projects/trafficserver/trunk/doxygen/>`_:
   Doxygen reference
-  `Type
   Index <http://ci.apache.org/projects/trafficserver/trunk/doxygen/classes.html>`_
-  :doc:`Sample Source Code <../sample-source-code.en>`
-  `Deprecated
   Functions <http://ci.apache.org/projects/trafficserver/trunk/doxygen/deprecated.html>`_

Below is a section-by-section breakdown of this guide:

-  :doc:`Getting Started <../getting-started.en>`
   How to compile and load plugins. Walks through a simple "hello
   world" example; explains how to initialize and register plugins.

-  :doc:`How to Create Traffic Server
   Plugins <../how-to-create-trafficserver-plugins.en>`
   Basic structures that all plugins use: events, continuations, and
   how to hook on to Traffic Server processes. Detailed explication of a
   sample blacklisting plugin.

-  :doc:`Remap Plugin <../remap-plugin.en>`
   This chapter demonstrates on a practical example how you can
   exploit the Traffic Server remap API for your plugins.

-  :doc:`Header-Based Plugin Examples <../header-based-plugin-examples.en>`
   Detailed explanation about writing plugins that work on HTTP
   headers; discusses sample blacklisting and basic authorization
   plugins.

-  :doc:`HTTP Transformation Plugins <../http-transformation-plugin.en>`
   Detailed explanation of the null-transform example; also discusses
   ``VConnections``, ``VIOs``, and IO buffers.

-  :doc:`New Protocol Plugins <../new-protocol-plugins.en>`
   Detailed explanation of a sample protocol plugin that supports a
   synthetic protocol. Discusses ``VConnections`` and mutexes, as well
   as the new ``NetConnection``, DNS lookup, logging, and cache APIs.

The remaining sections comprise the API function reference and are
organized by function type:

-  :doc:`Miscellaneous Interface Guide <../misc-interface-guide.en>`
   Details error-writing and tracing functions, thread functions, and
   Traffic Server API versions of the ``malloc`` and ``fopen`` families.
   The Traffic Server API versions overcome various C library
   limitations.

-  :doc:`HTTP Hooks and Transactions <../http-hooks-and-transactions.en>`
   Functions in this chapter hook your plugin to Traffic Server HTTP
   processes.

-  :doc:`HTTP Headers <../http-headers.en>`
   Contains instructions for implementing performance enhancements for
   all plugins that manipulate HTTP headers. These functions examine and
   modify HTTP headers, MIME headers, URLs, and the marshal buffers that
   contain header information. If you are working with headers, then be
   sure to read this chapter.

-  :doc:`Mutex Guide <../mutex-guide.en>`

-  :doc:`Continuations <../continuations.en>`
   Continuations provide the basic callback mechanism and data
   abstractions used in Traffic Server.

-  :doc:`Plugin Configurations <../plugin-configurations.en>`

-  :doc:`Actions Guide <../actions-guide.en>`
   Describes how to use ``TSActions`` and the ``TSDNSLookup`` API.

-  :doc:`IO Guide <../io-guide.en>`
   Describes how to use the Traffic Server IO interfaces:
   ``TSVConnection``, ``TSVIO``, ``TSIOBuffer``, ``TSNetVConnection``,
   the Cache API.

-  :doc:`Plugin Management <../plugin-management.en>`
   These functions enable you to set up a configuration interface for
   plugins, access installed plugin files, and set up plugin licensing.

-  :doc:`Adding Statistics <../adding-statistics.en>`
   These functions add statistics to your plugin.

-  `Function
   Index <http://ci.apache.org/projects/trafficserver/trunk/doxygen/>`_
   Doxygen generated Traffic Server API Documentation


