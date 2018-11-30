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

TSContCall
**********

Synopsis
========

`#include <ts/ts.h>`

.. function:: int TSContCall(TSCont contp, TSEvent event, void * edata)

Description
===========

Call the continuation :arg:`contp` as if from a hook with the event type :arg:`event` and data of
:arg:`edata`. Presuming :arg:`contp` was created in a manner like::

   TSContCreate(CallbackHandler, TSMutexCreate());

Therefore there is a function::

   int CallbackHandler(TSCont this, TSEvent id, void * data);

As a result :func:`TSContCall` will effectively do::

   return CallbackHandler(contp, event, edata);

If there is a mutex associated with :arg:`contp`, :func:`TSComtCall` assumes that mutex is held already.
:func:`TSContCall` will directly call the handler associated with the continuation.  It will return the
value returned by the handler in :arg:`contp`.

If :arg:`contp` has a mutex, the plugin must acquire the lock on the mutex for :arg:`contp` before calling
:func:`TSContCall`. See :func:`TSContMutexGet` and :func:`TSMutexLockTry` for mechanisms for doing this. 

The most common case is the code called by :func:`TSContCall` must complete before further code is executed
at the call site. An alternative approach to handling the locking directly would be to split the call site
into two continuations, one of which is signalled (possibly via :func:`TSContCall`) from the original 
:func:`TSContCall` target.

Note mutexes returned by :func:`TSMutexCreate` are recursive mutexes, therefore if the lock is
already held on the thread of execution acquiring the lock again is very fast. Mutexes are also
shareable so that the same mutex can be used for multiple continuations.::

   TSMutex mutex = TSMutexCreate();
   TSCont cont1 = TSContCreate(Handler1, mutex);
   TSCont cont2 = TSContCreate(Handler2, mutex);

In this example case, :code:`cont1` can assume the lock for :code:`cont2` is held. This should be
considered carefully because for the same reason any thread protection between the continuations is
removed. This works well for tightly coupled continuations that always operate in a fixed sequence.

