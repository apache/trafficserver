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

TSHttpTxnResponseActionGet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpTxnResponseActionGet(TSHttpTxn txnp, TSResponseAction *action)

Description
===========

Gets the ResponseAction set by a plugin.

The action is an out-param and must point to a valid location

The returned action.hostname must not be modified, and is owned by some plugi
n if not null.

The action members will always be zero, if no plugin has called TSHttpTxnResp
onseActionSet.

.. cpp:type:: uint16_t in_port_t

   A type representing a port number.

.. struct:: TSResponseAction

   Exposed for custom parent selection behavior.

   .. member:: char const *hostname

      The host for next request.

   .. member:: size_t hostname_len

      The host length for next request (not including null).

   .. member:: in_port_t port

      The port for next request.
