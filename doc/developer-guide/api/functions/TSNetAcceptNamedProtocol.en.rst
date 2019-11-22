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

TSNetAcceptNamedProtocol
************************

Listen on all SSL ports for connections for the specified protocol name.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSNetAcceptNamedProtocol(TSCont contp, const char * protocol)

Description
===========

:type:`TSNetAcceptNamedProtocol` registers the specified :arg:`protocol`
for all statically configured TLS ports.  When a client using the TLS
Next Protocol Negotiation extension negotiates the requested protocol,
|TS| will route the request to the given handler :arg:`contp`.

.. note::

   Be aware that the protocol is not registered on ports opened by
   other plugins.

The event and data provided to the handler are the same as for
:func:`TSNetAccept`.  If a connection is successfully accepted, the
event code will be :data:`TS_EVENT_NET_ACCEPT` and the event data
will be a valid :type:`TSVConn` bound to the accepted connection.

.. important::

   Neither :arg:`contp` nor :arg:`protocol` are copied. They must remain valid
   for the lifetime of the plugin.

:type:`TSNetAcceptNamedProtocol` fails if the requested protocol
cannot be registered on all of the configured TLS ports. If it fails,
the protocol will not be registered on any ports. There is no partial failure.
