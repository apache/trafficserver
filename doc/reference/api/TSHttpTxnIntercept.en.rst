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


TSHttpTxnIntercept
==================

Allows a plugin take over the servicing of the request as though it
was the origin server.


Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: void TSHttpTxnIntercept(TSCont contp, TSHttpTxn txnp)


Description
-----------

contp will be sent :c:data:`TS_EVENT_NET_ACCEPT`.  The edata passed
with :c:data:`TS_NET_EVENT_ACCEPT` is an :c:type:`TSVConn` just as it
would be for a normal accept.  The plugin must act as if it is an http
server and read the http request and body off the :c:type:`TSVConn`
and send an http response header and body.

:c:func:`TSHttpTxnIntercept` must be called be called from only
:c:data:`TS_HTTP_READ_REQUEST_HOOK`.  Using
:c:type:`TSHttpTxnIntercept` will bypass the Traffic Server cache.  If
response sent by the plugin should be cached, use
:c:func:`TSHttpTxnServerIntercept` instead.
:c:func:`TSHttpTxnIntercept` primary use is allow plugins to serve
data about their functioning directly.

:c:func:`TSHttpTxnIntercept` must only be called once per transaction.
