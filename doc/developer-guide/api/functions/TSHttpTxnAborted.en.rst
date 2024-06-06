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

TSHttpTxnAborted
================

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnAborted(TSHttpTxn txnp, bool *client_abort)

Description
-----------

:func:`TSHttpTxnAborted` returns :enumerator:`TS_SUCCESS` if the requested
transaction is aborted. This function should be used to determine whether
a transaction has been aborted before attempting to cache the results.

Broadly, transaction aborts can be classified into either client side aborts or
server side. To distinguish between these, we have another boolean parameter
which gets set to TRUE in case of client side aborts.

Return values
-------------

The API returns :enumerator:`TS_SUCCESS`, if the requested transaction is aborted,
:enumerator:`TS_ERROR` otherwise.
