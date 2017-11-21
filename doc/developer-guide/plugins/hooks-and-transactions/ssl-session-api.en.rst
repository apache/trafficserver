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

.. _developer-plugins-ssl-session-hooks:

.. default-domain:: c

TLS Session Plugin API
**********************

These interfaces enable a plugin to hook into operations on the ATS TLS session cache.  ATS also provides API's
to enable the plugin to update the session cache based on outside information, e.g. peer servers.

.. macro:: TS_SSL_SESSION_HOOK

This hook is invoked when a change has been made to the ATS session cache or a session has been accessed
from ATS via openssl.  These hooks are only activated if the ATS implementation of the session cache is in
use.  This means :ts:cv:`proxy.config.ssl.session_cache` has been set to 2.

The hook callback has the following signature

.. function:: int SSL_session_callback(TSCont contp, TSEvent event, void * edata)

The edata parameter is a pointer to a :type:`TSSslSessionID`.

This callback in synchronous since the underlying openssl callback is unable to pause processing.

The following events can be sent to this callback

.. macro:: TS_EVENT_SSL_SESSION_NEW

   Sent after a new session has been inserted into the SSL session cache.  The plugin can call :func:`TSSslSessionGet` to retrieve the actual session object.  The plugin could communicate information about the new session to other processes or update additional logging or statistics.

.. macro:: TS_EVENT_SSL_SESSION_GET

   Sent after a session has been fetched from the SSL session cache by a client request.  The plugin could update additional logginc and statistics.

.. macro:: TS_EVENT_SSL_SESSION_REMOVE

   Sent after a session has been removed from the SSL session cache.  The plugin could communication information about the session removal to other processes or update additional logging and statistics.

Utility Functions
******************

A number of API functions will likely be used with this hook.

* :func:`TSSslSessionGet`
* :func:`TSSslSessionGetBuffer`
* :func:`TSSslSessionInsert`
* :func:`TSSslSessionRemove`
* :func:`TSSslTicketKeyUpdate`

Example Use Case
****************

Consider deploying a set of ATS servers as a farm behind a layer 4 load balancer.  The load balancer does not
guarantee that all the requests from a single client are directed to the same ATS box.  Therefore, to maximize TLS session
reuse, the servers should share session state via some external communication library like redis or rabbitmq.

To do this, they write a plugin that sets the :macro:`TS_SSL_SESSION_HOOK`.  When the hook is triggered, the plugin function sends the
updated session state to the other ATS servers via the communication library.

The plugin also has thread that listens for updates and calls :func:`TSSslSessionInsert` and :func:`TSSslSessionRemove` to update the local session cache accordingly.

The plugin can also engage in a protocol to periodically update the session ticket encryption key and communicate the new key to its
peers.  The plugin calls :func:`TSSslTicketKeyUpdate` to update the local ATS process with the newest keys and the last N keys.
