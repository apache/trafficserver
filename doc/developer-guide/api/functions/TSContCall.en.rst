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

:func:`TSContCall` will check :arg:`contp` for a mutex. If there is a mutex an attempt will be made
to lock that mutex. If either there is no mutex, or the mutex lock was acquired, the handler will be
called directly. Otherwise there is a mutex and it was not successfully locked. In that case an
event will be scheduled to dispatch as soon as possible, but not in the current call stack. The
nature of event dispatch means the event will not be dispatched until the mutext can be locked. In
all cases the handler in :arg:`contp` will be called with the same arguments. :func:`TSContCall`
will return 0 if a mutex was present but the lock was not acquired. Otherwise it will return the
value returned by the handler in :arg:`contp`.

If the scheduling behavior of :func:`TSContCall` isn't appropriate, either :arg:`contp` must not
have a mutex, or the plugin must acquire the lock on the mutex for :arg:`contp` before calling
:func:`TSContCall`. See :func:`TSContMutexGet` and :func:`TSMutexLockTry` for mechanisms for doing
the latter. This is what :func:`TSContCall` does internally, and should be done by the plugin only
if a different approach for waiting for the lock is needed. The most common case is the code called
by :func:`TSContCall` must complete before further code is executed at the call site. An alternative
approach to handling the locking directly would be to split the call site in to two continuations,
one of which is signalled (possibly via :func:`TSContCall`) from the original :func:`TSContCall`
target.

Note mutexes returned by :func:`TSMutexCreate` are recursive mutexes, therefore if the lock is
already held on the thread of execution acquiring the lock again is very fast. Mutexes are also
shareable so that the same mutex can be used for multiple continuations.::

   TSMutex mutex = TSMutexCreate();
   TSCont cont1 = TSContCreate(Handler1, mutex);
   TSCont cont2 = TSContCreate(Handler2, mutex);

In this example case, :code:`cont1` can assume the lock for :code:`cont2` is held. This should be
considered carefully because for the same reason any thread protection between the continuations is
removed. This works well for tightly coupled continuations that always operate in a fixed sequence.

