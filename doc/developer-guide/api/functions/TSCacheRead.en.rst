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

TSCacheRead
***********

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAction TSCacheRead(TSCont contp, TSCacheKey key)

Description
===========

Asks the Traffic Server cache if the object corresponding to :arg:`key` exists
in the cache and can be read.

If the object can be read, the Traffic Server cache calls the continuation
:arg:`contp` back with the event :data:`TS_EVENT_CACHE_OPEN_READ`. In this case,
the cache also passes :arg:`contp` a cache vconnection and :arg:`contp` can then
initiate a read operation on that vconnection using :type:`TSVConnRead`.

If the object cannot be read, the cache calls :arg:`contp` back with the event
:data:`TS_EVENT_CACHE_OPEN_READ_FAILED`. The user (:arg:`contp`) has the option to
cancel the action returned by :type:`TSCacheRead`. Note that reentrant calls
are possible, i.e. the cache can call back the user (:arg:`contp`) in the same
call.
