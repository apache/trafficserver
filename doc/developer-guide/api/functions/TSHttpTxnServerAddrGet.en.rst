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

TSHttpTxnServerAddrGet
**********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: sockaddr const* TSHttpTxnServerAddrGet(TSHttpTxn txnp)

Description
===========

Get the origin server address for transaction :arg:`txnp`.

.. note::

   The pointer is valid only for the current callback. Clients that
   need to keep the value across callbacks must maintain their own
   storage.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSHttpTxnServerAddrSet(3ts)`
