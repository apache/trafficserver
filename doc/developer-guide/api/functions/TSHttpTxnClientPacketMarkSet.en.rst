.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed with
   this work for additional information regarding copyright ownership.
   The ASF licenses this file to you under the Apache License, Version
   2.0 (the "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: cpp

TSHttpTxnClientPacketMarkSet
****************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnClientPacketMarkSet(TSHttpTxn txnp, int mark)

Description
===========

Change the packet firewall :arg:`mark` for the client side connection. The
entire firewall mark is replaced with :arg:`mark`, which is interpreted as a
32-bit unsigned bit pattern.

Returns :const:`TS_SUCCESS` when the client connection was modified, and
:const:`TS_ERROR` when there is no client connection to modify.

.. note::

   The firewall mark is only honored on platforms whose OS supports it,
   specifically Linux via ``SO_MARK``. On platforms without ``SO_MARK`` support
   the call still returns :const:`TS_SUCCESS` when a client connection is
   present, but setting the mark has no effect at the OS layer (it is a safe
   no-op).

.. note::

   The change takes effect immediately on the live client connection.

See Also
========

.. _Traffic Shaping:
                 https://cwiki.apache.org/confluence/display/TS/Traffic+Shaping
   :ts:cv:`proxy.config.net.sock_packet_mark_in` and TS-1090
