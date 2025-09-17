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

TSHttpTxnVerifiedAddrSet
************************

Sets a client IP address verified by a plugin.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnVerifiedAddrSet(TSHttpTxn txnp, const struct sockaddr *addr)

Description
===========

This function enables plugins to provide a reliable client IP address for |TS| and other plugins.
This is useful if there is a proxy in front of |TS| and it forwards client's IP address by HTTP header field, PROXY protocol, etc.
Plugins that call this function are expected to check the validity of the IP address.

The provided address will be used if :ts:cv:`proxy.config.acl.subjects` is set to `PLUGIN`.
Plugins can get the provided address by calling the getter function below if those need client's real IP address.

The address `addr` is internally copied and |TS| core maintains its own copy.
Plugins that call this function do not need to keep the original for later use.


TSHttpTxnVerifiedAddrGet
************************

Gets the client IP address verified by a plugin.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnVerifiedAddrGet(TSHttpTxn txnp, const struct sockaddr **addr)

Description
===========

This is the getter version of the above setter. This returns 1 if a verified address is available.
Please note that a port number is not always available even if the function returns 1.

The address returned is maintained by |TS| core. Plugins cannot free the memory.
