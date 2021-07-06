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

TSCacheWrite
************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAction TSCacheWrite(TSCont contp, TSCacheKey key)

Description
===========

Asks the Traffic Server cache if :arg:`contp` can start writing the object
corresponding to :arg:`key` to the cache.

If the object can be written, the cache calls :arg:`contp` back with the
event :data:`TS_EVENT_CACHE_OPEN_WRITE`.  In this case, the cache
also passes :arg:`contp` a cache vconnection and :arg:`contp` can then initiate a
write operation on that vconnection using :type:`TSVConnWrite`.  The
object is not committed to the cache until the vconnection is closed.
When all data has been transferred, the user (:arg:`contp`) must do an
:type:`TSVConnClose`.  In case of any errors, the user must do an
``TSVConnAbort(contp, 0)``.

If the object cannot be written, the cache calls :arg:`contp` back with the
event :data:`TS_EVENT_CACHE_OPEN_WRITE_FAILED`.  This can happen,
for example, if there is another object with the same :arg:`key` being
written to the cache.  The user (:arg:`contp`) has the option to cancel the
action returned by :type:`TSCacheWrite`.

Note that reentrant calls are possible, i.e. the cache can call back
the user (:arg:`contp`) in the same call.
