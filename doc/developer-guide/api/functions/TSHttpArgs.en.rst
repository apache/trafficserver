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

TSHttpArgs
************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpArgIndexReserve(const char * name, const char * description, int * arg_idx)
.. function:: TSReturnCode TSHttpArgIndexNameLookup(const char * name, int * arg__idx, const char ** description)
.. function:: TSReturnCode TSHttpArgIndexLookup(int arg_idx, const char ** name, const char ** description)
.. function:: void TSHttpTxnArgSet(TSHttpTxn txnp, int arg_idx, void * arg)
.. function:: void * TSHttpTxnArgGet(TSHttpTxn txnp, int arg_idx)
.. function:: void TSHttpSsnArgSet(TSHttpTxn txnp, int arg_idx, void * arg)
.. function:: void * TSHttpSsnArgGet(TSHttpTxn txnp, int arg_idx)

Description
===========

|TS| sessions and transactions provide a fixed array of void pointers that can be used by plugins to
store information. This can be used to avoid creating a per session or transaction continuations to
hold data, or to communicate between plugins as the values in the array are visible to any plugin
which can access the session or transaction. The array values are opaque to |TS| and it will not
dereference or release them. Plugins are responsible for cleaning up any resources pointed to by the
values or, if the values are simply values there is no need for the plugin to remove them after the
session or transaction has completed.

To avoid collisions between plugins a plugin should first *reserve* an index in the array by calling
:func:`TSHttpArgIndexReserve` passing it an identifying name, a description, and a pointer to an
integer which will get the reserved index. The function returns :code:`TS_SUCCESS` if an index was
reserved, :code:`TS_ERROR` if not (most likely because all of the indices have already been
reserved). Generally this will be a file or library scope global which is set at plugin
initialization. This function is used in the example remap plugin :ts:git:`example/remap/remap.cc`.
The index is stored in the global :code:`arg_index`. When an index is reserved it is reserved for
both sessions and transactions.

To look up the owner of a reserved index use :func:`TSHttpArgIndexNameLookup`. If the :arg:`name` is
found as an owner, the function returns :code:`TS_SUCCESS` and :arg:`arg_index` is updated with the
index reserved under that name. If :arg:`description` is not :code:`NULL` then it will be updated
with the description for that reserved index. This enables communication between plugins where
plugin "A" reserves an index under a well known name and plugin "B" locates the index by looking it
up under that name.

The owner of a reserved index can be found with :func:`TSHttpArgIndexLookup`. If :arg:`arg_index` is
reserved then the function returns :code:`TS_SUCCESS` and :arg:`name` and :arg:`description` are
updated. :arg:`name` must point at a valid character pointer but :arg:`description` can be
:code:`NULL`.

Manipulating the array is simple. :func:`TSHttpTxnArgSet` sets the array slot at :arg:`arg_idx` for
the transaction :arg:`txnp` to the value :arg:`arg`. Note this sets the value only for the specific
transaction. Similarly :func:`TSHttpSsnArgSet` sets the value for a session argument. The values can
be retrieved with :func:`TSHttpTxnArgGet` for transactions and :func:`TSHttpSsnArgGet` for sessions,
which return the specified value. Values that have not been set are :code:`NULL`.

.. note:: Session arguments persist for the entire session, which means potentially across all transactions in that session.

.. note:: Following arg index reservations is conventional, it is not enforced.
