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

TSContSchedule
**************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSAction TSContSchedule(TSCont contp, ink_hrtime delay, TSThreadPool tp)

Description
===========

Schedules :arg:`contp` to run :arg:`delay` milliseconds in the future. This is approximate. The delay
will be at least :arg:`delay` but possibly more. Resultions finer than roughly 5 milliseconds will
not be effective. :arg:`contp` is required to have a mutex, which is provided to
:func:`TSContCreate`.

The return value can be used to cancel the scheduled event via :func:`TSActionCancel`. This is
effective until the continuation :arg:`contp` is being dispatched. However, if it is scheduled on
another thread this can problematic to be correctly timed. The return value can be checked with
:func:`TSActionDone` to see if the continuation ran before the return, which is possible if
:arg:`delay` is `0`.

The continuation is scheduled for a particular thread selected from a group of similar threads, as indicated by :arg:`tp`.

=========================== =======================================================================================
Pool                        Properties
=========================== =======================================================================================
``TS_THREAD_POOL_DEFAULT``  Use the default pool. Continuations using this must not block.
``TS_THREAD_POOL_NET``      Transaction processing threads. Continuations on these threads must not block.
``TS_THREAD_POOL_TASK``     Background threads. Continuations can perform blocking operations.
``TS_THREAD_POOL_SSL``      *DEPRECATED* - these are no longer used as of ATS 6.
``TS_THREAD_POOL_DNS``      DNS request processing. May not exist depending on configuration. Not recommended.
``TS_THREAD_POOL_REMAP``    *DEPRECATED* - these are not longer used.
``TS_THREAD_POOL_CLUSTER``  *DEPRECATED* - these are no longer used as of ATS 7.
``TS_THREAD_POOL_UDP``      *DEPRECATED*
=========================== =======================================================================================

In practice, any choice except ``TS_THREAD_POOL_NET`` or ``TS_THREAD_POOL_TASK`` is strong not
recommended. The ``TS_THREAD_POOL_NET`` threads are the same threads on which callback hooks are
called and continuations that use them have the same restrictions. ``TS_THREAD_POOL_TASK`` threads
are threads that exist to perform long or blocking actions, although sufficiently long operation can
impact system performance by blocking other continuations on the threads.
