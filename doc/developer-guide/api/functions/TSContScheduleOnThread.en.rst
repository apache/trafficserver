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

TSContScheduleOnThread
**********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAction TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread)

Description
===========

Mostly the same as :func:`TSContSchedule`. Schedules :arg:`contp` on :arg:`ethread`.

Note that the TSContSchedule() family of API shall only be called from an ATS EThread.
Calling it from raw non-EThreads can result in unpredictable behavior.

See Also
========

:doc:`TSContSchedule.en`
:doc:`TSContScheduleOnPool.en`
