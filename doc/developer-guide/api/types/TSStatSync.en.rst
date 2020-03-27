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

TSStatSync
*****************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. c:type:: TSStatSync

Enum typedef.

Enumeration Members
===================

.. c:member:: TSStatSync TS_STAT_SYNC_SUM

   This stat sync type should be used for gauge metrics (i.e can increase or decrease with time). It may be manipulated using TSStatIntIncrement, TSStatIntDecrement, TSStatIntSet. E.g for counting number of available origin-servers or number of active threads.

.. c:member:: TSStatSync TS_STAT_SYNC_COUNT

   This stat sync type should be used for counter metrics (i.e it should only increase with time). It should only be manipulated using TSStatIntIncrement. E.g for tracking call counts or uptime.

.. c:member:: TSStatSync TS_STAT_SYNC_AVG

   Values should be arithmetically averaged.

.. c:member:: TSStatSync TS_STAT_SYNC_TIMEAVG

   Values should be arithmetically averaged over a time period.

Description
===========

The level of persistence for a statistic value.
