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

TSContScheduleOnPool
********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAction TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp)

Description
===========

Schedules :arg:`contp` to run :arg:`timeout` milliseconds in the future, on a random thread that
belongs to :arg:`tp`. The :arg:`timeout` is an approximation, meaning it will be at least
:arg:`timeout` milliseconds but possibly more. Resolutions finer than roughly 5 milliseconds will
not be effective. Note that :arg:`contp` is required to have a mutex, which is provided to
:func:`TSContCreate`.

The continuation is scheduled for a particular thread selected from a group of similar threads,
as indicated by :arg:`tp`. If :arg:`contp` already has an thread affinity set, and the thread
type of thread affinity is the same as :arg:`tp`, then :arg:`contp` will be scheduled on the
thread specified by thread affinity.

=========================== =======================================================================================
Pool                        Properties
=========================== =======================================================================================
``TS_THREAD_POOL_NET``      Transaction processing threads. Continuations on these threads must not block.
``TS_THREAD_POOL_TASK``     Background threads. Continuations can perform blocking operations.
``TS_THREAD_POOL_DNS``      DNS request processing. May not exist depending on configuration. Not recommended.
``TS_THREAD_POOL_UDP``      UDP processing.
=========================== =======================================================================================

In practice, any choice except ``TS_THREAD_POOL_NET`` or ``TS_THREAD_POOL_TASK`` is strongly not
recommended. The ``TS_THREAD_POOL_NET`` threads are the same threads on which callback hooks are
called and continuations that use them have the same restrictions. ``TS_THREAD_POOL_TASK`` threads
are threads that exist to perform long or blocking actions, although sufficiently long operation can
impact system performance by blocking other continuations on the threads.

The return value can be used to cancel the scheduled event via :func:`TSActionCancel`. This is
effective until the continuation :arg:`contp` is being dispatched. However, if it is scheduled on
another thread this can be problematic to be correctly timed. The return value can be checked with
:func:`TSActionDone` to see if the continuation ran before the return, which is possible if
:arg:`timeout` is `0`.

If :arg:`contp` has no thread affinity set, the thread it is now scheduled on will be set
as its thread affinity thread.

Note that the TSContSchedule() family of API shall only be called from an ATS EThread.
Calling it from raw non-EThreads can result in unpredictable behavior.

Example Scenarios
=================

Scenario 1 (no thread affinity info, different types of threads)
----------------------------------------------------------------

When thread affinity is not set, a plugin calls the API on thread "A" (which is an "ET_TASK" type), and
wants to schedule on an "ET_NET" type thread provided in "tp", the system would see there is no thread
affinity information stored in "contp."

In this situation, system sees there is no thread affinity information stored in "contp". It then
checks whether the type of thread "A" is the same as provided in "tp", and sees that "A" is "ET_TASK",
but "tp" says "ET_NET". So "contp" gets scheduled on the next available "ET_NET" thread provided by a
round robin list, which we will call thread "B". Since "contp" doesn't have thread affinity information,
thread "B" will be assigned as the affinity thread for it automatically.

The reason for doing this is most of the time people want to schedule the same things on the same type
of thread, so logically it is better to default the first thread that it is scheduled on as the affinity
thread.

Scenario 2 (no thread affinity info, same types of threads)
-----------------------------------------------------------

Slight variation of scenario 1, instead of scheduling on a "ET_NET" thread, the plugin wants to schedule
on a "ET_TASK" thread (i.e. "tp" contains "ET_TASK" now), all other conditions stays the same.

This time since the type of the desired thread for scheduling and thread "A" are the same, the system
schedules "contp" on thread "A", and assigns thread "A" as the affinity thread for "contp".

The reason behind this choice is that we are trying to keep things simple such that lock contention
problems happens less. And for the most part, there is no point of scheduling the same thing on several
different threads of the same type, because there is no parallelism between them (a thread will have to
wait for the previous thread to finish, either because locking or the nature of the job it’s handling is
serialized since its on the same continuation).

Scenario 3 (has thread affinity info, different types of threads)
-----------------------------------------------------------------

Slight variation of scenario 1, thread affinity is set for continuation "contp" to thread "A", all other
conditions stays the same.

In this situation, the system sees that the "tp" has "ET_NET", but the type of thread "A" is "ET_TASK".
So even though "contp" has an affinity thread, the system will not use that information since the type is
different, instead it schedules "contp" on the next available "ET_NET" thread provided by a round robin
list, which we will call thread "B". The difference with scenario 1 is that since thread "A" is set to
be the affinity thread for "contp" already, the system will NOT overwrite that information with thread "B".

Most of the time, a continuation will be scheduled on one type of threads, and rarely gets scheduled on
a different type. But when that happens, we want it to return to the thread it was previously on, so it
won’t have any lock contention problems. And that’s also why "thread_affinity" is not a hashmap of thread
types and thread pointers.

Scenario 4 (has thread affinity info, same types of threads)
------------------------------------------------------------

Slight variation of scenario 3, the only difference is "tp" now says "ET_TASK".

This is the easiest scenario since the type of thread "A" and "tp" are the same, so the system schedules
"contp" on thread "A". And, as discussed, there is really no reason why one may want to schedule
the same continuation on two different threads of the same type.

.. note:: In scenario 3 & 4, it doesn't matter which thread the plugin is calling the API from.


See Also
========

:doc:`TSContScheduleEveryOnPool.en`
:doc:`TSContScheduleOnThread.en`
