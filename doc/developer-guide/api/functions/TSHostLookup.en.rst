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

TSHostLookup
************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAction TSHostLookup(TSCont contp, const char * hostname, size_t namelen)

Description
===========

Attempts host name resolution for the given :arg:`hostname` with length :arg:`namelen`. When a result is ready the handler of :arg:`contp` is called with the :macro:`TS_EVENT_HOST_LOOKUP` event and a :type:`TSHostLookupResult`. Use :func:`TSHostLookupResultAddrGet` to retrieve the resulting address from the :type:`TSHostLookupResult`.

A call to :func:`TSHostLookup` may be synchronous—in which case the handler for :arg:`contp` will be called with the answer before the call to :func:`TSHostLookup` returns—or the call to :func:`TSHostLookup` may be asynchronous—in which case it returns immediately with a :type:`TSAction`, and the handler :arg:`contp` will be called in the future. See :doc:`../../plugins/actions/index.en` for guidance.

In particular, :arg:`contp` must have been created with a :type:`TSMutex`; see :func:`TSContCreate`
