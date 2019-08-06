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

TSVConnProtocolEnable/Disable
*****************************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSVConnProtocolEnable(TSVConn vconn, const char* protocol)
.. function:: TSReturnCode TSVConnProtocolDisable(TSVConn vconn, const char* protocol)

Description
===========

:func:`TSVConnProtocolEnable` will enable the protocol specified by :arg:`protocol` to be advertised in the TLS protocol negotiation.

Similarly, :func:`TSVConnProtocolDisable` will remove the protocol specified by :arg:`protocol` from the TLS protocol negotiation.

To be effective, these calls must be made from the early TLS negotiation hooks like :member:`TS_SSL_CLIENT_HELLO_HOOK` or :member:`TS_SSL_SERVERNAME_HOOK`.

Examples
========

The example below is excerpted from `example/plugins/c-api/disable_http2/disable_http2.cc`
in the Traffic Server source distribution. It shows how the :func:`TSVConnProtocolDisable` function
can be used in a plugin called from the :member:`TS_SSL_SERVERNAME_HOOK`.

.. literalinclude:: ../../../../example/plugins/c-api/disable_http2/disable_http2.cc
  :language: c
  :lines: 41-54

