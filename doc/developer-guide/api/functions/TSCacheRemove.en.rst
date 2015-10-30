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

TSCacheRemove
*************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSAction TSCacheRemove(TSCont contp, TSCacheKey key)

Description
===========

Removes the object corresponding to :arg:`key` from the cache.

If the object was removed successfully, the cache calls :arg:`contp` back with the
event :data:`TS_EVENT_CACHE_REMOVE`. If the object was not found in the cache,
the cache calls :arg:`contp` back with the event
:data:`TS_EVENT_CACHE_REMOVE_FAILED`.

In both of these callbacks, the user (:arg:`contp`) does not have to do
anything.  The user does not get any vconnection from the cache, since
no data needs to be transferred.  When the cache calls :arg:`contp` back with
:data:`TS_EVENT_CACHE_REMOVE`, the remove has already been commited.
