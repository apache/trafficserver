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

.. default-domain:: cpp

TSHttpSsnClientAddrGet
**********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: struct sockaddr const * TSHttpSsnClientAddrGet(TSHttpSsn ssnp)

Description
===========

Return the socket address of the client for the HTTP session :arg:`ssnp`.
The returned pointer references storage owned by |TS| and is only valid
for the duration of the current callback; plugins that need to keep the
value across callbacks must copy it into their own storage.

This is the session-level counterpart of :func:`TSHttpTxnClientAddrGet`
and is appropriate when the caller has a session handle but no specific
transaction (for example, in session-level hooks).

If the listener that accepted the connection has the ``pp-clnt`` flag set
and a PROXY Protocol header was successfully parsed, the returned address
is the PROXY-Protocol source address rather than the immediate TCP peer.
Without ``pp-clnt`` the returned address is the immediate TCP peer even
when PROXY Protocol is enabled. See :ref:`Proxy Protocol <proxy-protocol>`
for the full enumeration of surfaces gated by ``pp-clnt``.

Return Value
============

A pointer to the client address, or ``nullptr`` if :arg:`ssnp` is invalid
or no client address is available.

See Also
========

:manpage:`TSAPI(3ts)`,
:func:`TSHttpTxnClientAddrGet`,
:func:`TSNetVConnClientAddrGet`
