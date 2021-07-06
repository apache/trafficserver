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

TSVConnArgs
************

Synopsis
========

.. note::

   This set of API is obsoleted as of ATS v9.0.0, and will be removed with ATS v10.0.0!
   For details of the new APIs, see :ref:`tsuserargs`.


.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSVConnArgIndexReserve(const char * name, const char * description, int * arg_idx)
.. function:: TSReturnCode TSVConnArgIndexNameLookup(const char * name, int * arg_idx, const char ** description)
.. function:: TSReturnCode TSVConnArgIndexLookup(int arg_idx, const char ** name, const char ** description)
.. function:: void TSVConnArgSet(TSVConn vc, int arg_idx, void * arg)
.. function:: void * TSVConnArgGet(TSVConn vc, int arg_idx)

Description
===========

Virtual connection objects (API type :c:type:`TSVConn`) support an array of :code:`void *` values that
are controlled entirely by plugins. These are not used in any way by the core. This allows plugins
to store data associated with a specific virtual connection for later retrieval by the same plugin
in a different hook or by another plugin. Because the core does not interact with these values any
cleanup is the responsibility of the plugin.

To avoid collisions between plugins a plugin should first *reserve* an index in the array by calling
:func:`TSVConnArgIndexReserve` passing it an identifying name, a description, and a pointer to an
integer which will get the reserved index. The function returns :code:`TS_SUCCESS` if an index was
reserved, :code:`TS_ERROR` if not (most likely because all of the indices have already been
reserved). Generally this will be a file or library scope global which is set at plugin
initialization. Note the reservation is by convention - nothing stops a plugin from interacting with
a :code:`TSVConn` arg it has not reserved.

To look up the owner of a reserved index use :func:`TSVConnArgIndexNameLookup`. If the :arg:`name` is
found as an owner, the function returns :code:`TS_SUCCESS` and :arg:`arg_index` is updated with the
index reserved under that name. If :arg:`description` is not :code:`nullptr` then it will be updated
with the description for that reserved index. This enables communication between plugins where
plugin "A" reserves an index under a well known name and plugin "B" locates the index by looking it
up under that name.

The owner of a reserved index can be found with :func:`TSVConnArgIndexLookup`. If :arg:`arg_index` is
reserved then the function returns :code:`TS_SUCCESS` and :arg:`name` and :arg:`description` are
updated. :arg:`name` must point at a valid character pointer but :arg:`description` can be
:code:`nullptr`.

Manipulating the array is simple. :func:`TSVConnArgSet` sets the array slot at :arg:`arg_idx` for
the :arg:`vc` to the value :arg:`arg`. Note this sets the value only for the specific
:c:type:`TSVConn`. The values can be retrieved with :func:`TSVConnArgGet` which returns the
specified value. Values that have not been set are :code:`nullptr`.

Hooks
=====

Although these can be used from any hook that has access to a :c:type:`TSVConn` it will generally be
the case that :func:`TSVConnArgSet` will be used in early intervention hooks and
:func:`TSVConnArgGet` in session / transaction hooks. Cleanup should be done on the
:code:`TS_VCONN_CLOSE_HOOK`.

.. rubric:: Appendix

.. note::
   This is originally from `Issue 2388 <https://github.com/apache/trafficserver/issues/2388>`__. It
   has been extended based on discussions with Kees and Leif Hedstrom at the ATS summit.
