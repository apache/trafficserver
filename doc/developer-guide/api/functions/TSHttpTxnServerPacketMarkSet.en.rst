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

TSHttpTxnServerPacketMarkSet
****************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnServerPacketMarkSet(TSHttpTxn txnp, int mark)

Description
===========

Change the packet firewall :arg:`mark` for the server side (origin) connection.
The entire firewall mark is replaced with :arg:`mark`, which is interpreted as a
32-bit unsigned bit pattern.

Always returns :const:`TS_SUCCESS`, including when no server connection has been
established yet.

.. note::

   The firewall mark is only honored on platforms whose OS supports it,
   specifically Linux via ``SO_MARK``. On platforms without ``SO_MARK`` support
   the call still returns :const:`TS_SUCCESS`, but setting the mark has no effect
   at the OS layer (it is a safe no-op).

.. note::

   If a live server connection exists, the mark is applied to it immediately; the
   mark is also recorded on the transaction so that any subsequent server
   connection for this transaction uses it.

See Also
========

.. _Traffic Shaping:
                 https://cwiki.apache.org/confluence/display/TS/Traffic+Shaping
   :ts:cv:`proxy.config.net.sock_packet_mark_out` and TS-1090
