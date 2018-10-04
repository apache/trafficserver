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

`#include <ts/ts.h>`

.. function:: TSAction TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp)

Description
===========

Mostly the same as :func:`TSContSchedule`. Schedules :arg:`contp` on a random thread that belongs to :arg:`tp`.
If thread type of the thread specified by thread affinity is the same as :arg:`tp`, the :arg:`contp` will
be scheduled on the thread specified by thread affinity.

The continuation is scheduled for a particular thread selected from a group of similar threads, as indicated by :arg:`tp`.

=========================== =======================================================================================
Pool                        Properties
=========================== =======================================================================================
``TS_THREAD_POOL_NET``      Transaction processing threads. Continuations on these threads must not block.
``TS_THREAD_POOL_TASK``     Background threads. Continuations can perform blocking operations.
``TS_THREAD_POOL_SSL``      *DEPRECATED* - these are no longer used as of ATS 6.
``TS_THREAD_POOL_DNS``      DNS request processing. May not exist depending on configuration. Not recommended.
``TS_THREAD_POOL_REMAP``    *DEPRECATED* - these are no longer used.
``TS_THREAD_POOL_CLUSTER``  *DEPRECATED* - these are no longer used as of ATS 7.
``TS_THREAD_POOL_UDP``      *DEPRECATED*
=========================== =======================================================================================

In practice, any choice except ``TS_THREAD_POOL_NET`` or ``TS_THREAD_POOL_TASK`` is strongly not
recommended. The ``TS_THREAD_POOL_NET`` threads are the same threads on which callback hooks are
called and continuations that use them have the same restrictions. ``TS_THREAD_POOL_TASK`` threads
are threads that exist to perform long or blocking actions, although sufficiently long operation can
impact system performance by blocking other continuations on the threads.

See Also
========

:doc:`TSContSchedule.en`
:doc:`TSContScheduleOnThread.en`
