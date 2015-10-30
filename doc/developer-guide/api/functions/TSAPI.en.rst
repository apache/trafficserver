.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

.. _developer-tsapi:

TSAPI
*****

Introduction to the Apache Traffic Server API.

Synopsis
========

`#include <ts/ts.h>`
`#include <ts/remap.h>`

Description
===========

The Apache Traffic Server API enables you to create plugins, using
the C programming language, that customize the behavior of your
Traffic Server installation.

Traffic Server enables sophisticated caching and processing of
web-related traffic, such as DNS and HTTP requests and responses.
Traffic Server itself consists of an event-driven loop that can be
simplified as follows::

    for (;;) {
        event = get_next_event();
        handle_event (event);
    }

You compile your plugin source code to create a shared library that
Traffic Server loads when it is started. Your plugin contains
callback functions that are registered for specific Traffic Server
events. When Traffic Server needs to process an event, it invokes
any and all call-back functions you've registered for that event
type.

Possible uses for plugins include the following:

* HTTP processing plugins can filter, blacklist, authorize users or transform content.

* Protocol plugins can enable Traffic Server to proxy-cache new protocol content.

* A blacklisting plugin denies attempts to access web sites that are off-limits.

* Append transform plugins add data to HTTP response content.

* An image conversion plugin transforms JPEG images to GIF images.

* Compression plugins send response content to a compression server
  that compresses the data (alternatively, a compression library local
  to the Traffic Server host machine could do the compression).

* Authorization plugins check a user's permissions to access
  particular web sites. The plugin could consult a local authorization
  program or send queries to an authorization server.

* A plugin that gathers client information from request headers
  and enters this information in a database.

* A protocol plugin listen for specific protocol requests on a
  designated port and then uses Traffic Server's proxy server and
  cache to serve client requests.

.. _developer-tsapi-naming-conventions:

Naming Conventions
==================

The Traffic Server API adheres to the following naming conventions:

* The TS prefix is used for all function and variable names defined
  in the Traffic Server API. For example, :data:`TS_EVENT_NONE`, :type:`TSMutex`,
  and :func:`TSContCreate`.

* Enumerated values are always written in all uppercase letters.
  For example, :data:`TS_EVENT_NONE` and :data:`TS_VC_CLOSE_ABORT`.

* Constant values are all uppercase; enumerated values can be seen
  as a subset of constants. For example, :data:`TS_URL_SCHEME_FILE` and
  :data:`TS_MIME_FIELD_ACCEPT`.

* The names of defined types are mixed-case. For example, :type:`TSHttpSsn`
  and :func:`TSHttpTxn`. :func:`TSDebug`

* Function names are mixed-case. For example, :func:`TSUrlCreate`
  and :func:`TSContDestroy`.

* Function names use the following subject-verb naming style:
  TS-<subject>-<verb>, where <subject> goes from general to specific.
  This makes it easier to determine what a function does by reading
  its name. For example, the function to retrieve the password field
  (the specific subject) from a URL (the general subject) is
  :func:`TSUrlPasswordGet`.

* Common verbs like Create, Destroy, Get, Set, Copy, Find, Retrieve,
  Insert, Remove, and Delete are used only when appropriate.

.. _developer-tsapi-plugin-loading:

Plugin Loading and Configuration
================================

When Traffic Server is first started, it consults the plugin.config
file to determine the names of all shared plugin libraries that
need to be loaded. The plugin.config file also defines arguments
that are to be passed to each plugin's initialization function,
:func:`TSPluginInit`. The :file:`records.config` file defines the path to
each plugin shared library.

The sample :file:`plugin.config` file below contains a comment line, a blank
line, and two plugin configurations::

    # This is a comment line.
    my-plugin.so www.junk.com www.trash.com www.garbage.com
    some-plugin.so arg1 arg2 $proxy.config.http.cache.on

Each plugin configuration in the :file:`plugin.config` file resembles
a UNIX or DOS shell command; each line in :file:`plugin.config`
cannot exceed 1023 characters.

The first plugin configuration is for a plugin named my-plugin.so.
It contains three arguments that are to be passed to that plugin's
initialization routine. The second configuration is for a plugin
named some-plugin.so; it contains three arguments. The last argument,
$proxy.config.http.cache.on, is actually a configuration variable.
Traffic Server will look up the specified configuration variable
and substitute its value.

Plugins are loaded and initialized by Traffic Server in the order
they appear in the :file:`plugin.config` file.

.. _developer-tsapi-plugin-init:

Plugin Initialization
=====================

Each plugin must define an initialization function named
:func:`TSPluginInit` that Traffic Server invokes when the
plugin is loaded. :func:`TSPluginInit` is commonly used to
read configuration information and register hooks for event
notification.

Files
=====

:file:`plugin.config`,
:file:`records.config`

See Also
========

:manpage:`TSPluginInit(3ts)`
