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

TSHttpTxnIntercept
******************

Allows a plugin take over the servicing of the request as though it was the
origin server.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp)

Description
===========

:arg:`contp` will be sent :macro:`TS_EVENT_NET_ACCEPT`. The edata passed
with :macro:`TS_EVENT_NET_ACCEPT` is an :c:type:`TSVConn` just as it
would be for a normal accept. The plugin must act as if it is an HTTP
server and read the HTTP request and body off the :c:type:`TSVConn`
and send an HTTP response header and body.

:func:`TSHttpTxnIntercept` must be called be called from only
:data:`TS_HTTP_READ_REQUEST_HDR_HOOK`.  Using
:type:`TSHttpTxnIntercept` will bypass the Traffic Server cache.  If
response sent by the plugin should be cached, use
:func:`TSHttpTxnServerIntercept` instead.
:func:`TSHttpTxnIntercept` primary use is allow plugins to serve
data about their functioning directly.

:func:`TSHttpTxnIntercept` must only be called once per transaction.
