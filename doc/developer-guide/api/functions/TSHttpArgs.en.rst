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
**********

Synopsis
========

.. note::

   This set of API is obsoleted as of ATS v9.0.0, and will be removed with ATS v10.0.0!
   For details of the new APIs, see :ref:`tsuserargs`.


.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnArgIndexReserve(const char * name, const char * description, int * arg_idx)
.. function:: TSReturnCode TSHttpTxnArgIndexNameLookup(const char * name, int * arg__idx, const char ** description)
.. function:: TSReturnCode TSHttpTxnArgIndexLookup(int arg_idx, const char ** name, const char ** description)
.. function:: TSReturnCode TSHttpSsnArgIndexReserve(const char * name, const char * description, int * arg_idx)
.. function:: TSReturnCode TSHttpSsnArgIndexNameLookup(const char * name, int * arg__idx, const char ** description)
.. function:: TSReturnCode TSHttpSsnArgIndexLookup(int arg_idx, const char ** name, const char ** description)
.. function:: void TSHttpTxnArgSet(TSHttpTxn txnp, int arg_idx, void * arg)
.. function:: void * TSHttpTxnArgGet(TSHttpTxn txnp, int arg_idx)
.. function:: void TSHttpSsnArgSet(TSHttpSsn ssnp, int arg_idx, void * arg)
.. function:: void * TSHttpSsnArgGet(TSHttpSsn ssnp, int arg_idx)

Description
===========

|TS| sessions and transactions provide a fixed array of void pointers that can be used by plugins to
store information. This can be used to avoid creating a per session or transaction continuations to
hold data, or to communicate between plugins as the values in the array are visible to any plugin
which can access the session or transaction. The array values are opaque to |TS| and it will not
dereference nor release them. Plugins are responsible for cleaning up any resources pointed to by the
values or, if the values are simply values, there is no need for the plugin to remove them after the
session or transaction has completed.

To avoid collisions between plugins a plugin should first *reserve* an index in the array. A
transaction based plugin argument is reserved by calling :func:`TSHttpTxnArgIndexReserve`. A session
base plugin argument is reserved by calling :func:`TSHttpSsnArgIndexReserve`. Both functions have the arguments

:arg:`name`
   An identifying name for the plugin that reserved the index. Required.

:arg:`description`
   An optional description of the use of the arg. This can be :code:`nullptr`.

:arg:`arg_idx`
   A pointer to an :code:`int`. If an index is successfully reserved, the :code:`int` pointed at by this is
   set to the reserved index. It is not modified if the call is unsuccessful.

The functions return :code:`TS_SUCCESS` if an index was reserved,
:code:`TS_ERROR` if not (most likely because all of the indices have already been reserved).
Generally this will be a file or library scope global which is set at plugin initialization. This
function is used in the example remap plugin :ts:git:`example/plugins/c-api/remap/remap.cc`. The index is stored
in the plugin global :code:`arg_index`. Transaction and session plugin argument indices are reserved
independently.

To look up the owner of a reserved index use :func:`TSHttpTxnArgIndexNameLookup` or
:func:`TSHttpSsnArgIndexNameLookup` for transaction and session plugin argument respectively. If
:arg:`name` is found as an owner, the function returns :code:`TS_SUCCESS` and :arg:`arg_index` is
updated with the index reserved under that name. If :arg:`description` is not :code:`NULL` then
the character pointer to which it points will be updated to point at the description for that
reserved index. This enables communication between plugins where plugin "A" reserves an index under
a well known name and plugin "B" locates the index by looking it up under that name.

The owner of a reserved index can be found with :func:`TSHttpTxnArgIndexLookup` or
:func:`TSHttpSsnArgIndexLookup` for transaction and session plugin arguments respectively. If
:arg:`arg_index` is reserved then the function returns :code:`TS_SUCCESS` and the pointers referred
to by :arg:`name` and :arg:`description` are updated. :arg:`name` must point at a valid character
pointer but :arg:`description` can be :code:`NULL` in which case it is ignored.

Manipulating the array is simple. :func:`TSHttpTxnArgSet` sets the array slot at :arg:`arg_idx` for
the transaction :arg:`txnp` to the value :arg:`arg`. Note this sets the value only for the specific
transaction. Similarly :func:`TSHttpSsnArgSet` sets the value for a session argument. The values can
be retrieved with :func:`TSHttpTxnArgGet` for transactions and :func:`TSHttpSsnArgGet` for sessions,
which return the specified value. Values that have not been set are :code:`NULL`.

.. note:: Session arguments persist for the entire session, which means potentially across all transactions in that session.

.. note:: Following arg index reservations is conventional, it is not enforced.
