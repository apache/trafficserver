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
.. default-domain:: cpp
.. _admin-stats-core-eventloop:

Event Loop
**********

The core of network processing in |TS| is the :term:`event loop`. This loop executes any pending
events and performs I/O operations on network connections. The time spent in the event loop is a
critical component of latency as any network connection has at most one I/O operation per loop. The
general mechanism is

*  Dispatch any pending events.

*  Check for any pending I/O activity. This waits a variable amount of time, but at most
   :ts:cv:`proxy.config.thread.max_heartbeat_mseconds` milliseconds. It is reduced to wake up before
   the next scheduled event. Although this is done in milliseconds, system timers are
   rarely that accurate.

*  For each network connection that has pending I/O, dispatch an event to the corresponding network
   virtual connection object.

*  Dispatch any events generated while handling I/O in the previous steps.

Event loops that take a long time will have a noticeable impact on transaction latency. There are a
number of statistics gathered to help determine if this is the problem. Because instantaneous values
are not useful, the data is gathered in three different time buckets - 10, 100, and 1000 seconds.
There is a parallel set of statistics for each, and each larger time period includes the smaller
ones. The statistic values are all "for this time period". For example, the statistic
"proxy.process.eventloop.count.100s" is "the number of event loops executed in the last 100
seconds". The histogram metrics provide a more detailed look at the distribution of event loop times
to distinguish whether long times are common or an outlier.

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

.. ts:stat:: global proxy.process.eventloop.drain.queue.max.10s integer
   :units: nanoseconds

    The maximum amount of time spent draining the event queue in a single loop in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.io.wait.max.10s integer
   :units: nanoseconds

    The maximum amount of time spent waiting for network IO in a single loop in the last 10 seconds.

.. ts:stat:: global proxy.process.eventloop.io.work.max.10s integer
   :units: nanoseconds

    The maximum amount of time spent processing network IO in a single loop in the last 10 seconds.

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

.. ts:stat:: global proxy.process.eventloop.drain.queue.max.100s integer
   :units: nanoseconds

    The maximum amount of time spent draining the event queue in a single loop in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.io.wait.max.100s integer
   :units: nanoseconds

    The maximum amount of time spent waiting for network IO in a single loop in the last 100 seconds.

.. ts:stat:: global proxy.process.eventloop.io.work.max.100s integer
   :units: nanoseconds

    The maximum amount of time spent processing network IO in a single loop in the last 100 seconds.

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

.. ts:stat:: global proxy.process.eventloop.drain.queue.max.1000s integer
   :units: nanoseconds

    The maximum amount of time spent draining the event queue in a single loop in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.io.wait.max.1000s integer
   :units: nanoseconds

    The maximum amount of time spent waiting for network IO in a single loop in the last 1000 seconds.

.. ts:stat:: global proxy.process.eventloop.io.work.max.1000s integer
   :units: nanoseconds

    The maximum amount of time spent processing network IO in a single loop in the last 1000 seconds.

.. rubric:: Histogram Metrics

.. ts:stat:: global proxy.process.eventloop.time.*ms integer

    This is a set of statistics that provide a histogram of event loop times. Each is labeled
    with a value indicating the minimum time for the bucket. The maximum is less than the minimum
    of the next bucket. E.g. there is a "10ms" and a "20ms" statistic. The "10ms" statistic counts
    the number of times the event loop took at least 10ms and less than 20ms. The exception is the
    last bucket which has no maximum. The histogram is semi-logarithmic so that bucket sizes increase
    exponentially with smaller intermediate steps. Every 60 seconds the accumulated values are
    decreased by half to provide an exponential decay of recent activity. The current set of
    statistics is ::

        proxy.process.eventloop.time.0ms 2210
        proxy.process.eventloop.time.5ms 293
        proxy.process.eventloop.time.10ms 1848
        proxy.process.eventloop.time.15ms 1483
        proxy.process.eventloop.time.20ms 0
        proxy.process.eventloop.time.25ms 0
        proxy.process.eventloop.time.30ms 0
        proxy.process.eventloop.time.35ms 348
        proxy.process.eventloop.time.40ms 0
        proxy.process.eventloop.time.50ms 0
        proxy.process.eventloop.time.60ms 23872
        proxy.process.eventloop.time.70ms 0
        proxy.process.eventloop.time.80ms 0
        proxy.process.eventloop.time.100ms 0
        proxy.process.eventloop.time.120ms 0
        proxy.process.eventloop.time.140ms 0
        proxy.process.eventloop.time.160ms 0
        proxy.process.eventloop.time.200ms 0
        proxy.process.eventloop.time.240ms 0
        proxy.process.eventloop.time.280ms 0
        proxy.process.eventloop.time.320ms 0
        proxy.process.eventloop.time.400ms 0
        proxy.process.eventloop.time.480ms 0
        proxy.process.eventloop.time.560ms 0
        proxy.process.eventloop.time.640ms 0
        proxy.process.eventloop.time.800ms 0
        proxy.process.eventloop.time.960ms 0
        proxy.process.eventloop.time.1120ms 0
        proxy.process.eventloop.time.1280ms 0
        proxy.process.eventloop.time.1600ms 0
        proxy.process.eventloop.time.1920ms 0
        proxy.process.eventloop.time.2240ms 0
        proxy.process.eventloop.time.2560ms 0

    This is an idle instance therefore the values are clustered strongly on 0 (less than 5ms) and
    60ms (the default wait for I/O timeout).

.. ts:stat:: global proxy.process.api.time.*ms integer

   A set of statistics that provide a histogram of total time spent in plugins. This is sampled
   per plugin, rather than the aggregate value for milestone :enumerator:`TS_MILESTONE_PLUGIN_TOTAL`.

   See :ts:stat:`proxy.process.eventloop.time.*ms` for technical details.
