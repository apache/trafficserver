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

.. _tsuserargs:

TSUserArgs
**********

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

    typedef enum {
      TS_USER_ARGS_TXN,   ///< Transaction based.
      TS_USER_ARGS_SSN,   ///< Session based
      TS_USER_ARGS_VCONN, ///< VConnection based
      TS_USER_ARGS_GLB,   ///< Global based
      TS_USER_ARGS_COUNT  ///< Fake enum, # of valid entries.
    } TSUserArgType;

.. function::  TSReturnCode TSUserArgIndexReserve(TSUserArgType type, const char *name, const char *description, int *arg_idx)
.. function::  TSReturnCode TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description)
.. function::  TSReturnCode TSUserArgIndexLookup(TSUserArgType type, int arg_idx, const char **name, const char **description)
.. function::  void TSUserArgSet(void *data, int arg_idx, void *arg)
.. function::  void *TSUserArgGet(void *data, int arg_idx)

Description
===========

|TS| sessions, transactions, virtual connections and globally provide a fixed array of void pointers
that can be used by plugins to store information. This can be used to avoid creating a per session or
transaction continuations to hold data, or to communicate between plugins as the values in the array
are visible to any plugin which can access the session or transaction. The array values are opaque
to |TS| and it will not dereference nor release them. Plugins are responsible for cleaning up any
resources pointed to by the values or, if the values are simply values, there is no need for the plugin
to remove them after the session or transaction has completed.

To avoid collisions between plugins a plugin should first *reserve* an index in the array. A
plugin can reserve a slot of a particular type by calling :func:`TSUserArgIndexReserve`. The arguments are:

:arg:`type`
   The type for which the plugin intend to reserve a slot. See :code:`TSUserArgType` above.

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

To look up the owner of a reserved index use :func:`TSUserArgIndexNameLookup`, with the appropriate type.
If :arg:`name` is found as an owner, the function returns :code:`TS_SUCCESS` and :arg:`arg_index` is
updated with the index reserved under that name. If :arg:`description` is not :code:`NULL` then
the character pointer to which it points will be updated to point at the description for that
reserved index. This enables communication between plugins where plugin "A" reserves an index under
a well known name and plugin "B" locates the index by looking it up under that name.

The owner of a reserved index can be found with :func:`TSUserArgIndexLookup`. If
:arg:`arg_index` is reserved then the function returns :code:`TS_SUCCESS` and the pointers referred
to by :arg:`name` and :arg:`description` are updated. :arg:`name` must point at a valid character
pointer but :arg:`description` can be :code:`NULL` in which case it is ignored.

Manipulating the array is simple. :func:`TSUserArgSet` sets the array slot at :arg:`arg_idx`, for the
particular type based on the provide data pointer. The values can be retrieved with the value from
:func:`TSUserArgGet`. Values that have not been set are :code:`NULL`. Note that both the setter and the getter are
context sensitive, based on the type (or value) of the data pointer:

   ============== =======================================================================
   data type      Semantics
   ============== =======================================================================
   ``TSHttpTxn``  The implicit context is for a transaction (``TS_USER_ARGS_TXN``)
   ``TSHttpSsn``  The implicit context is for a transaction (``TS_USER_ARGS_SSN``)
   ``TSVConn``    The implicit context is for a transaction (``TS_USER_ARGS_VCONN``)
   ``nullptr``    The implicit context is global (``TS_USER_ARGS_GLB``)
   ============== =======================================================================

Note that neither :func:`TSUserArgSet` nor :func:`TSUserArgGet` has any type safety on the :arg:`data`
parameters, being a ``void*`` pointer.


.. note:: Session arguments persist for the entire session, which means potentially across all transactions in that session.

.. note:: Following arg index reservations is conventional, it is not enforced.
