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

.. default-domain:: c

TSHttpTxnServerPacketTosSet
***************************

Change packet TOS for the server side connection.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnServerPacketTosSet(TSHttpTxn txnp, int tos)

Description
===========

.. note::

   The change takes effect immediately. If no OS connection has been
   made, then this sets the mark that will be used. If an OS connection
   is established

   TOS is deprecated and replaced by DSCP. This is still used to set
   DSCP, however the first 2 bits of this value will be ignored as they
   now belong to the ECN field.

.. XXX Third sentence in the first paragraph above needs to be completed.

See Also
========

.. _Traffic Shaping:
                 https://cwiki.apache.org/confluence/display/TS/Traffic+Shaping
   :ts:cv:`proxy.config.net.sock_packet_tos_out` and TS-1090
