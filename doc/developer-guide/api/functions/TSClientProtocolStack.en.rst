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

TSClientProtocolStack
*********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnClientProtocolStackGet(TSHttpTxn txnp, int n, char const** result, int* actual)

.. function:: TSReturnCode TSHttpSsnClientProtocolStackGet(TSHttpSsn ssnp, int n, char const** result, int* actual)

.. function:: char const* TSHttpTxnClientProtocolStackContains(TSHttpTxn txnp)

.. function:: char const* TSHttpSsnClientProtocolStackContains(TSHttpSsn ssnp)

.. function:: char const* TSNormalizedProtocolTag(char const* tag)

.. function:: char const* TSRegisterProtocolTag(char const* tag)

Description
===========

These functions are used to explore the protocol stack of the client (user agent) connection to
|TS|. The functions :func:`TSHttpTxnClientProtocolStackGet` and
:func:`TSHttpSsnClientProtocolStackGet` can be used to retrieve the entire protocol stack for the
user agent connection. :func:`TSHttpTxnClientProtocolStackContains` and
:func:`TSHttpSsnClientProtocolStackContains` will check for a specific protocol :arg:`tag` being
present in the stack.

Each protocol is represented by tag which is a null terminated string. A particular tag will always
be returned as the same character pointer and so protocols can be reliably checked with pointer
comparisons. :func:`TSNormalizedProtocolTag` will return this character pointer for a specific
:arg:`tag`. A return value of :const:`NULL` indicates the provided :arg:`tag` is not registered as
a known protocol tag. :func:`TSRegisterProtocolTag` registers the :arg:`tag` and then returns its
normalized value. This is useful for plugins that provide custom protocols for user agents.

The protocols are ordered from higher level protocols to the lower level ones on which the higher
operate. For instance a stack might look like "http/1.1,tls/1.2,tcp,ipv4". For
:func:`TSHttpTxnClientProtocolStackGet` and :func:`TSHttpSsnClientProtocolStackGet` these values
are placed in the array :arg:`result`. :arg:`count` is the maximum number of elements of
:arg:`result` that may be modified by the function call. If :arg:`actual` is not :const:`NULL` then
the actual number of elements in the protocol stack will be returned. If this is equal or less than
:arg:`count` then all elements were returned. If it is larger then some layers were omitted from
:arg:`result`. If the full stack is required :arg:`actual` can be used to resize :arg:`result` to
be sufficient to hold all of the elements and the function called again with updated :arg:`count`
and :arg:`result`. In practice the maximum number of elements will is almost certain to be less
than 10 which therefore should suffice. These functions return :const:`TS_SUCCESS` on success and
:const:`TS_ERROR` on failure which should only occurr if :arg:`txnp` or :arg:`ssnp` are invalid.

The :func:`TSHttpTxnClientProtocolStackContains` and :func:`TSHttpSsnClientProtocolStackContains`
functions are provided for the convenience when only the presence of a protocol is of interest, not
its location or the presence of other protocols. These functions return :const:`NULL` if the protocol
:arg:`tag` is not present, and a pointer to the normalized tag if it is present. The strings are
matched with an anchor prefix search, as with debug tags. For instance if :arg:`tag` is "tls" then it
will match "tls/1.2" or "tls/1.3". This makes checking for TLS or IP more convenient. If more precision
is required the entire protocol stack can be retrieved and processed more thoroughly.

.. _protocol_tags:

The protocol tags defined by |TS|.

=========== =========
Protocol    Tag
=========== =========
HTTP/1.1    http/1.1
HTTP/1.0    http/1.0
HTTP/2      h2
WebSocket   ws
TLS 1.3     tls/1.3
TLS 1.2     tls/1.2
TLS 1.1     tls/1.1
TLS 1.0     tls/1.0
TCP         tcp
UDP         udp
IPv4        ipv4
IPv6        ipv6
QUIC        quic
=========== =========

Examples
--------

The example below is excerpted from `example/protocol_stack/protocol_stack.cc`
in the Traffic Server source distribution. It demonstrates how to
use :func:`TSHttpTxnClientProtocolStackGet` and :func:`TSHttpTxnClientProtocolStackContains`

.. literalinclude:: ../../../../example/protocol_stack/protocol_stack.cc
  :language: c
  :lines: 31-46
