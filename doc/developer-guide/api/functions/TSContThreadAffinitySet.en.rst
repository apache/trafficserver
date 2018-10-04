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

TSContThreadAffinitySet
**************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSContThreadAffinitySet(TSCont contp, TSEventThread ethread)

Description
===========

Set the thread affinity of continuation :arg:`contp` to :arg:`ethread`. Future calls to
:func:`TSContSchedule`, and :func:`TSContScheduleOnPool` that has the same type as :arg:`ethread`
will schedule the continuation on :arg:`ethread`, rather than an arbitrary thread of that type.

Return Values
=============

:data:`TS_SUCCESS` if thread affinity of continuation :arg:`contp` was set to :arg:`ethread`,
:data:`TS_ERROR` if not.
