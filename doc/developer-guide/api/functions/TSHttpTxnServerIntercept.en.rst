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

.. default-domain:: c

TSHttpTxnServerIntercept
************************

Intercept origin server requests.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSHttpTxnServerIntercept(TSCont contp, TSHttpTxn txnp)

Description
===========

:func:`TSHttpTxnServerIntercept` allows a plugin take over the servicing of the
request as though it was the origin server. In the event a request needs to be
made to the server for transaction :arg:`txnp`, :arg:`contp` will be sent a
:macro:`TS_EVENT_NET_ACCEPT` event. The :arg:`edata` passed with
:macro:`TS_EVENT_NET_ACCEPT` is an :type:`TSVConn` just as it would be for a
normal accept. The plugin must act as if it is an HTTP server and read the HTTP
request and body from the :type:`TSVConn` and send an HTTP response header and
body.

:func:`TSHttpTxnServerIntercept` must be not be called after the connection to
the server has taken place. This means that the last hook that it can be called
from is :data:`TS_HTTP_READ_CACHE_HDR_HOOK`. If a connection to the server is
not necessary, the continuation :arg:`contp` will be sent a
:macro:`TS_EVENT_NET_ACCEPT_FAILED` event when the transaction completes.

The response from the plugin is cached subject to standard and configured HTTP
caching rules. Should the plugin wish the response not be cached, the plugin
must use appropriate HTTP response headers to prevent caching. The primary
purpose of :func:`TSHttpTxnServerIntercept` is allow plugins to provide gateways
to other protocols or to allow to plugin to its own transport for the next hop
to the server. :func:`TSHttpTxnServerIntercept` overrides parent cache
configuration.

:func:`TSHttpTxnServerIntercept` must only be called once per transaction. The
continuation :arg:`contp` must have been created with a valid :type:`TSMutex`.

See Also
========

:manpage:`TSAPI(3ts)`
