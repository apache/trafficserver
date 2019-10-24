.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../../../common.defs

.. _admin-stats-core-eventloop:

Event Loop
**********

The core of network processing in |TS| is the :term:`event loop`. This loop executes any pending
events and performs I/O operations on network connections. The time spent in the event loop is a
critical component of latency as any network connection has at most one I/O operation per loop. The
general mechanism is

*  Dispatch any pending events.

*  Check for any pending I/O activity. This wait a variable amount of time. It is at most
   :ts:cv:`proxy.config.thread.max_heartbeat_mseconds` milliseconds. It is reduced to the amount of
   time until the next scheduled event. Although this is done in milliseconds, system timers are
   rarely that accurate.

*  For each network connection dispatch an event to the corresponding network virtual connection
   object.

*  Dispatch any events generated while handling I/O in the previous step

Event loops that take a long time will have a noticeable impact on transaction latency. There are a
number of statistics gathered to help determine if this is the problem. Because instantaneous values
are not useful, the data is gathered in three different time buckets - 10, 100, and 1000 seconds.
There is a parallel set of statistics for each, and each larger time period includes the smaller
ones. The statistic values are all "for this time period". For example, the statistic
"proxy.process.eventloop.count.100s" is "the number of event loops executed in the last 100
seconds".

In general, the maximum loop time will create a floor under response latency. If that is frequently
high then it is likely transactions are experiencing significant latency.

.. rubric:: 10 Second Metrics

.. ts:stat:: global proxy.process.eventloop.count.10s integer

    Number of event loops executed in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.events.10s integer

    Number of events dispatched in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.events.min.10s integer

    Minimum number of events dispatched in a single loop in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.events.max.10s integer

    Maximum number of events dispatched in a single loop in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.wait.10s integer

    Number of loops in which the I/O activity check was done with a non-zero time out in the last 10
    seconds.

.. ts:stat:: global proxy.process.eventloop.time.min.10s integer
   :units: nanoseconds

    The minimum amount of time spent in a single loop in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.time.max.10s integer
   :units: nanoseconds

    The maximum amount of time spent in a single loop in the last 10 seconds.

.. rubric:: 100 Second Metrics

.. ts:stat:: global proxy.process.eventloop.count.100s integer

    Number of event loops executed in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.events.100s integer

    Number of events dispatched in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.events.min.100s integer

    Minimum number of events dispatched in a single loop in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.events.max.100s integer

    Maximum number of events dispatched in a single loop in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.wait.100s integer

    Number of loops in which the I/O activity check was done with a non-zero time out in the last 100
    seconds.

.. ts:stat:: global proxy.process.eventloop.time.min.100s integer
    :units: nanoseconds

    The minimum amount of time spent in a single loop in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.time.max.100s integer
    :units: nanoseconds

    The maximum amount of time spent in a single loop in the last 100 seconds.

.. rubric:: 1000 Second Metrics

.. ts:stat:: global proxy.process.eventloop.count.1000s integer

    Number of event loops executed in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.events.1000s integer

    Number of events dispatched in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.events.min.1000s integer

    Minimum number of events dispatched in a single loop in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.events.max.1000s integer

    Maximum number of events dispatched in a single loop in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.wait.1000s integer

    Number of loops in which the I/O activity check was done with a non-zero time out in the last 1000
    seconds.

.. ts:stat:: global proxy.process.eventloop.time.min.1000s integer
    :units: nanoseconds

    The minimum amount of time spent in a single loop in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.time.max.1000s integer
    :units: nanoseconds

    The maximum amount of time spent in a single loop in the last 1000 seconds.
