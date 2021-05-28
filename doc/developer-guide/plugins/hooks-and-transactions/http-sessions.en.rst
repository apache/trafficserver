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

.. _developer-plugins-hooks-http-sessions:

HTTP Sessions
*************

An **HTTP session** is an object that is defined for the lifetime of a
client's TCP session. The Traffic Server API enables you to add a global
hook to the start or end of an HTTP session, as well as add session
hooks that call back your plugin for every transaction within a given
session. When a client connects to Traffic Server, it opens up a TCP
connection and sends one or more HTTP requests. An individual request
and its response comprise the HTTP transaction. The **HTTP session**
begins when the client opens the connection and ends when the connection
closes.

The HTTP session hooks are:

-  ``TS_HTTP_SSN_START_HOOK`` Called when an HTTP session is started (a
   session starts when a client connects to Traffic Server). This hook
   must be added as a global hook.

-  ``TS_HTTP_SSN_CLOSE_HOOK`` Called when an HTTP session ends (a
   session ends when the client connection is closed). This hook must be
   added as a global hook.  The relative order of invocation between the
   ``TS_VCONN_CLOSE_HOOK`` and ``TS_HTTP_SSN_CLOSE_HOOK`` is undefined.  In
   most cases the ``TS_VCONN_CLOSE_HOOK`` will execute first, but that is
   not guaranteed.

Use the session hooks to get a handle to a session (an ``TSHttpSsn``
object). If you want your plugin to be called back for each transaction
within the session, then use ``TSHttpSsnHookAdd``.

**Note:** you must re-enable the session with ``TSHttpSsnReenable`` after
processing a session hook.

The session hook functions are listed below:

-  :c:func:`TSHttpSsnHookAdd`
-  :c:func:`TSHttpSsnReenable`

