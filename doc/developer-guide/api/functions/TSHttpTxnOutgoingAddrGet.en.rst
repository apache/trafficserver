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

Local outbound address
========================

Get or set the local IP address for outbound connections.


Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: sockaddr const* TSHttpTxnOutgoingAddrGet(TSHttpTxn txnp)
.. c:function:: TSReturnCode TSHttpTxnOutgoingAddrSet(TSHttpTxn txnp, sockaddr const* addr)


Description
-----------

These functions concern the local IP address and port, that is the address and port on the |TS| side
of outbound connections (network connections *from* |TS| *to* another socket).

The address and optional the port can be set with :func:`TSHttpTxnOutgoingAddrSet`. This must be
done before the outbound connection is made, that is, earlier than the :macro:`TS_HTTP_SEND_REQUEST_HDR_HOOK`.
A good choice is the :macro:`TS_HTTP_POST_REMAP_HOOK`, since it is a hook that is always called, and it
is the latest hook that is called before the connection is made.
The :arg:`addr` must be populated with the IP address and port to be used. If the port is not
relevant it can be set to zero, which means use any available local port. This function returns
:macro:`TS_SUCCESS` on success and :macro:`TS_ERROR` on failure.

Even on a successful call to :func:`TSHttpTxnOutgoingAddrSet`, the local IP address may not match
what was passing :arg:`addr` if :ts:cv:`session sharing <proxy.config.http.server_session_sharing.match>` is enabled.

Conversely :func:`TSHttpTxnOutgoingAddrGet` retrieves the local address and must be called in the
:macro:`TS_HTTP_SEND_REQUEST_HDR_HOOK` or later, after the outbound connection has been established. It returns a
pointer to a :code:`sockaddr` which contains the local IP address and port. If there is no valid
outbound connection, :arg:`addr` will be :code:`NULL`. The returned pointer is a transient pointer
and must not be referenced after the callback in which :func:`TSHttpTxnOutgoingAddrGet` was called.
