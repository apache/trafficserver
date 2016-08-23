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

.. include:: ../../../common.defs

.. _developer-plugins-examples:

Example Plugins
***************

.. toctree::
   :maxdepth: 2

   basic-authorization/index.en
   blacklist/index.en
   query-remap/index.en

.. _developer-plugins-header-based-examples:

Header-Based Plugin Examples
============================

Header-based plugins read or modify the headers of HTTP messages that
Traffic Server sends and receives. Reading this chapter will help you to
understand the following topics:

-  Creating continuations for your plugins

-  Adding global hooks

-  Adding transaction hooks

-  Working with HTTP header functions

The two sample plugins discussed in this chapter are ``blacklist-1.c``
and ``basic-auth.c``.

Overview
--------

Header-based plugins take actions based on the contents of HTTP request
or response headers. Examples include filtering (on the basis of
requested URL, source IP address, or other request header), user
authentication, or user redirection. Header-based plugins have the
following common elements:

-  The plugin has a static parent continuation that scans all Traffic
   Server headers (either request headers, response headers, or both).

-  The plugin has a global hook. This enables the plugin to check all
   transactions to determine if the plugin needs to do something.

-  The plugin gets a handle to the transaction being processed through
   the global hook.

-  If the plugin needs to do something to transactions in specific
   cases, then it sets up a transaction hook for a particular event.

-  The plugin obtains client header information and does something based
   on that information.

This chapter demonstrates how these components are implemented in SDK
sample code.

