.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

.. _ts-lifecycle-hook-add:

TSLifecycleHookAdd
******************

Synopsis
========

.. code-block:: c

    #include <ts/ts.h>

.. function:: void TSLifecycleHookAdd(TSLifecycleHookID id, TSCont contp)

Description
===========

:func:`TSLifecycleHookAdd` adds :arg:`contp` to the list of lifecycle hooks
specified by :arg:`id`. Lifecycle hooks are based on the Traffic Server
process, not on any specific transaction or session. These will typically be
called only once during the execution of the Traffic Server process and
therefore should be added in :func:`TSPluginInit` (which could itself be
considered a lifecycle hook). Unlike other hooks, lifecycle hooks may not have a
well defined ordering and use of them should not assume that one of the hooks
is always called before another unless specifically mentioned.

Types
=====

.. cpp:enum:: TSLifecycleHookID

   Life cycle hook selector.

   .. cpp:enumerator:: TS_LIFECYCLE_PORTS_INITIALIZED_HOOK

      Called after the :ts:cv:`proxy server port <proxy.config.http.server_ports>`
      data structures have been initialized but before connections are accepted on
      those ports. The sockets corresponding to the ports may or may not be open
      depending on how the :program:`traffic_server` process was invoked. Other
      API functions that depend on server ports should be called from this hook
      and not :func:`TSPluginInit`.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_PORTS_INITIALIZED` and
      ``NULL`` data.

   .. cpp:enumerator:: TS_LIFECYCLE_PORTS_READY_HOOK

      Called after enabling connections on the proxy server ports. Because |TS| is
      threaded this may or may not be called before any connections are accepted.
      The hook code may assume that any connection to |TS| started after this hook
      is called will be accepted by |TS|, making this a convenient place to signal
      external processes of that.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_PORTS_READY` and ``NULL``
      data.

   .. cpp:enumerator:: TS_LIFECYCLE_CACHE_READY_HOOK

      Called after |TS| cache initialization has finished.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_CACHE_READY` and ``NULL``
      data.

   .. cpp:enumerator:: TS_LIFECYCLE_MSG_HOOK

      Called when triggered by an external process, such as :program:`traffic_ctl`.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_MSG`. The data is an instance of the
      :c:type:`TSPluginMsg`. This contains a *tag* which is a null terminated string and a data payload.
      The payload cannot be assumed to be null terminated and is created by the external agent. Its internal
      structure and format are entirely under the control of the external agent although presumably there is
      an agreement between the plugin and the external where this is determined by the :arg:`tag`.

   .. cpp:enumerator:: TS_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED_HOOK

      Called after the initialization of the SSL context used by |TS| for outbound connections (|TS| as client).

   .. cpp:enumerator:: TS_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED_HOOK

      Called after every SSL context initialization used by |TS| for inbound connections (|TS| as the server).

   .. cpp:enumerator:: TS_LIFECYCLE_TASK_THREADS_READY_HOOK

      Called after |TS| task threads have been started.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_TASK_THREADS_READY` and ``NULL``
      data.

   .. cpp:enumerator:: TS_LIFECYCLE_SSL_SECRET_HOOK

      Called before the data for the certificate or key is loaded.  The data argument to the callback is a pointer to a :type:`TSSecretID` which
      contains a pointer to the name of the certificate or key and the relevant version if applicable.

      This hook gives the plugin a chance to load the certificate or key from an alternative source and set via the :c:func:`TSSslSecretSet` API.
      If there is no plugin override, the certificate or key will be loaded from disk and the secret name will be interpreted as a file path.

   .. cpp:enumerator:: TS_LIFECYCLE_SHUTDOWN_HOOK

      Called after |TS| receiving a shutdown signal, such as SIGTERM.

      Invoked with the event :c:data:`TS_EVENT_LIFECYCLE_SHUTDOWN` and ``NULL`` data.

.. c:struct:: TSPluginMsg

   The data for the plugin message event :c:data:`TS_EVENT_LIFECYCLE_MSG`.

   .. c:var:: const char * tag

      The tag of the message. This is a null terminated string.

   .. c:var:: const void * data

      Message data (payload). This is a raw slab of bytes - no structure is guaranteed.

   .. c:var:: size_t data_size

      The number of valid bytes pointed at by :var:`TSPluginMsg.data`.

Ordering
========

:cpp:enumerator:`TSLifecycleHookID::TS_LIFECYCLE_PORTS_INITIALIZED_HOOK` will always be called before
:cpp:enumerator:`TSLifecycleHookID::TS_LIFECYCLE_PORTS_READY_HOOK`.

Examples
========

The following example demonstrates how to correctly use
:func:`TSNetAcceptNamedProtocol`, which requires the proxy ports to be
initialized and therefore does not work if called from :func:`TSPluginInit`
directly.

.. code-block:: c

   #include <ts/ts.h>

   #define SSL_PROTOCOL_NAME "whatever"

   static int
   ssl_proto_handler(TSCont contp, TSEvent event, void* data)
   {
      /// Do named protocol handling.
   }

   static int
   local_ssl_init(TSCont contp, TSEvent event, void * edata)
   {
      if (TS_EVENT_LIFECYCLE_PORTS_INITIALIZED == event) { // just to be safe.
         TSNetAcceptNamedProtocol(
            TSContCreate(ssl_proto_handler, TSMutexCreate()),
            SSL_PROTOCOL_NAME
         );
      }
      return 0;
   }

   void
   TSPluginInit (int argc, const char * argv[])
   {
      TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, TSContCreate(local_ssl_init, NULL));
   }

History
=======

Lifecycle hooks were introduced to solve process initialization ordering issues
(TS-1487). Different API calls required different modules of |TS| to be
initialized for the call to work, but others did not work that late in
initialization, which was problematic because all of them could effectively
only be called from :func:`TSPluginInit` . The solution was to move
:func:`TSPluginInit` as early as possible in the process initialization and
provide hooks for API calls that needed to be invoked later which served
essentially as additional plugin initialization points.

See Also
========

:manpage:`TSAPI(3ts)`, :manpage:`TSContCreate(3ts)`
