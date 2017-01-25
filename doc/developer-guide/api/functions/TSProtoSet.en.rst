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

TSProtoSet
******************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSNextProtocolSet TSGetcloneProtoSet(TSAcceptor tna)
.. function:: TSNextProtocolSet TSUnregisterProtocol(TSNextProtocolSet protoset, const char* protocol)
.. function:: void TSRegisterProtocolSet(TSVConn sslp, TSNextProtocolSet ps)

Description
===========

:func:`TSGetcloneProtoSet` makes a copy of the ProtocolSet to be advertised by the ssl connection associated with :arg:`tna`. This function
returns :type:`TSNextProtocolSet` object which points to a clone of the protocolset owned by :arg:`tna`. This type represents the protocolset
containing the protocols which are advertised by an ssl connection during ssl handshake. Each :type:`TSAcceptor` object is associated with a protocolset.


:func:`TSUnregisterProtocol` unregisters :arg:`protocol` from :arg:`protoset` and returns the protocol set.
The returned protocol set needs to be registered with the :type:`TSVConn` using :func:`TSRegisterProtocolSet` that will advertise the protocols.


:func:`TSRegisterProtocolSet` registers :arg:`ps` with :arg:`sslp`. This function clears the protocolset string created by the already registered
protocolset before registering the new protocolset. On Success, the ssl object associated with :arg:`sslp` will then advertise the protocols contained in :arg:`ps`.
