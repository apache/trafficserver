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

TSContScheduleOnEntirePool
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: std::vector<TSAction> TSContScheduleOnEntirePool(TSCont contp, TSHRTime timeout, TSThreadPool tp)

Description
===========

Schedules :arg:`contp` to run :arg:`timeout` milliseconds in the future, on all threads that
belongs to :arg:`tp`. The :arg:`timeout` is an approximation, meaning it will be at least
:arg:`timeout` milliseconds but possibly more. Resolutions finer than roughly 5 milliseconds will
not be effective. Note that :arg:`contp` is required to NOT have a mutex, since the continuation
is scheduled on multiple threads. This means the continuation must handle synchronization itself.

The return value can be used to cancel the scheduled event via :func:`TSActionCancel`. This
is effective until the continuation :arg:`contp` is being dispatched. However, if it is
scheduled on another thread this can be problematic to be correctly timed. The return value
can be checked with :func:`TSActionDone` to see if the continuation ran before the return,
which is possible if :arg:`timeout` is `0`.

.. note:: Due to scheduling multiple events, the return value is changed to :type:`std::vector<TSAction>`, as compared to :type:`TSAction` of the other `TSContSchedule` APIs.

Note that the `TSContSchedule` family of API shall only be called from an ATS EThread.
Calling it from raw non-EThreads can result in unpredictable behavior.

See Also
========

:doc:`TSContScheduleOnPool.en`
:doc:`TSContScheduleOnThread.en`
:doc:`TSContScheduleEveryOnPool.en`
:doc:`TSContScheduleEveryOnThread.en`
:doc:`TSContScheduleEveryOnEntirePool.en`
