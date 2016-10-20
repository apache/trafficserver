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

TSHttpTxnServerAddrSet
**********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnServerAddrSet(TSHttpTxn txnp, struct sockaddr const* addr)

Description
===========

Set the origin server address for transaction :arg:`txnp`. This includes the port in :arg:`addr`.
The address family is also set by the contents of :arg:`addr`. The address data is copied out of
:arg:`addr` so there is no dependency on the lifetime of that object.

This hook must be called no later than TS_HTTP_OS_DNS_HOOK. If this
is called prior to TS_HTTP_OS_DNS_HOOK, DNS resolution will not be
done as the address of the server is already known.

Return Value
============

:data:`TS_ERROR` is returned if :arg:`addr` does not contain a valid
IPv4 or IPv6 address with a valid (non-zero) port.

Notes
=====

If |TS| is configured to retry connections to origin servers and
:func:`TSHttpTxnServerAddrGet` has been called, |TS| will return
to TS_HTTP_OS_DNS_HOOK so to let the plugin set a different server
address. Plugins should be prepared for TS_HTTP_OS_DNS_HOOK and any
subsequent hooks to be called multiple times.

Once a plugin calls :func:`TSHttpTxnServerAddrGet` any prior DNS
resolution results are lost. The plugin should use
:func:`TSHttpTxnServerAddrGet` to preserve any DNS Results that
might need.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSHttpTxnServerAddrGet(3ts)`
