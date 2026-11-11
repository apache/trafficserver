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

Watchdog
========

These statistics are produced by the event thread watchdog and are only updated when
:ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` is non-zero.

.. ts:stat:: global proxy.process.watchdog.max_awake_ms integer

   The longest time any event thread was continuously awake - that is, not back to sleeping on I/O -
   at any point since the previous watchdog scan.

   Each event loop records the awake span it just finished, and the scan takes the larger of that
   recorded peak and a live snapshot of any thread still awake, so a burst that began and ended
   between two scans is captured rather than missed. The value covers only the interval since the
   last scan; scans run every half of
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` milliseconds.

   Because of that, this is a gauge that is overwritten twice per scan period - a spike between two scrapes
   is gone by the time you look. Alert on ``proxy.process.watchdog.timeout_shrink_exceeded_count`` and
   ``proxy.process.watchdog.max_awake_ms_hwm`` for the crossings this will miss.

.. ts:stat:: global proxy.process.watchdog.max_awake_ms_hwm integer

   The largest value ``proxy.process.watchdog.max_awake_ms`` has ever reported since startup. Never
   decreases.

   This exists because the gauge is sampled far more often than it is scraped: at a two second threshold
   the watchdog publishes a fresh peak every second, so a single 30 second stall is visible to a 60
   second scrape only by luck. Use this to answer "what is the worst this process has served", and the
   gauge to answer "what is it doing right now".

.. ts:stat:: global proxy.process.watchdog.timeout_shrink_exceeded_count integer

   Number of watchdog scans whose peak awake span reached
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms`.

   Counted in every mode, including :ts:cv:`proxy.config.exec_thread.watchdog.mode` ``0``, where it is
   the primary signal - nothing else moves in report-only mode. This is the metric to alert on: it
   cannot be missed between scrapes, and unlike the ``engaged_count`` counters below, which only move
   when the state changes, its rate is meaningful. Scans run every half of that threshold, so a rate of
   two per second at a one second threshold means continuously saturated, while occasional single
   increments are isolated long loops.

.. ts:stat:: global proxy.process.watchdog.timeout_refuse_exceeded_count integer

   Number of watchdog scans whose peak awake span reached
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_refuse_ms`.

   The same measurement as ``proxy.process.watchdog.timeout_shrink_exceeded_count`` against the other
   threshold, and likewise counted in every mode - so you can see how often saturation would reach the
   depth at which accepts stop before turning
   :ts:cv:`proxy.config.exec_thread.watchdog.mode` ``1`` on. Stays at ``0`` while
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_refuse_ms` is ``0``, which is the default, since
   there is then no threshold to cross.

   Its ratio against the shrink counter is what to size the refuse threshold by: equal rates mean the
   two thresholds are too close together to give the accept queue a chance to bound inflow on its own.

.. ts:stat:: global proxy.process.watchdog.accept_shrink integer

   ``1`` while the accept queue shrink is engaged - that is, while the peak awake span has reached
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` - and ``0`` otherwise. Only ever
   non-zero when :ts:cv:`proxy.config.exec_thread.watchdog.mode` is ``1``.

   Stays ``1`` until ``proxy.process.watchdog.max_awake_ms`` has fallen all the way to the release
   deadband (:ts:cv:`proxy.config.exec_thread.watchdog.backlog_decrease_permille` of that threshold),
   so this reads engaged for as long as the peak sits between the two - back under the threshold but
   not yet visibly recovered.

.. ts:stat:: global proxy.process.watchdog.accept_shrink_engaged_count integer

   Number of times the shrink has been engaged since startup. A high rate here indicates the event
   threads are repeatedly saturating.

   If this climbs much faster than the overload episodes you can actually observe, backpressure is
   flapping - releasing on a lull and re-engaging on the next scan. Lowering
   :ts:cv:`proxy.config.exec_thread.watchdog.backlog_decrease_permille` deepens the release deadband
   and is what damps that.

.. ts:stat:: global proxy.process.watchdog.accept_refuse integer

   ``1`` while |TS| is not reading accepts at all - that is, while the peak awake span has reached
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_refuse_ms` - and ``0`` otherwise. Always ``0``
   when that setting is ``0``, which is the default, or when
   :ts:cv:`proxy.config.exec_thread.watchdog.mode` is not ``1``.

   Independent of ``proxy.process.watchdog.accept_shrink``: the two thresholds are separate, so
   either can read ``1`` with the other at ``0``, and both can be ``1`` at once - in which case the
   accept queue is bounded and nothing is draining it.

.. ts:stat:: global proxy.process.watchdog.accept_refuse_engaged_count integer

   Number of times |TS| has stopped reading accepts since startup. Compare it against
   ``accept_shrink_engaged_count`` to see how often saturation reaches the deeper of the two
   thresholds: if the two are equal, :ts:cv:`proxy.config.exec_thread.watchdog.timeout_refuse_ms` is
   close enough to :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` that the queue is
   never given a chance to bound inflow on its own.

.. ts:stat:: global proxy.process.net.watchdog_accepts_refused integer

   Number of accept passes that declined to drain their listen socket because the peak awake span had
   reached :ts:cv:`proxy.config.exec_thread.watchdog.timeout_refuse_ms`.

   This counts *passes*, not connections - a connection left queued is never accepted, so |TS| does
   not know how many were waiting. Read it as a measure of how often that state was in effect, and use
   ``proxy.process.watchdog.accept_refuse`` for whether it is in effect right now.

   The exempt loopback companion listeners always drain, so local connections are not counted here.

.. ts:stat:: global proxy.process.watchdog.listen_backlog_permille integer

   Current size of the non-exempt listen sockets' accept queues, in permille of
   :ts:cv:`proxy.config.net.listen_backlog`. ``1000`` when unthrottled.

   This is the control variable the watchdog adjusts once the peak awake span reaches
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` - decreasing multiplicatively while the
   event threads are saturated
   (:ts:cv:`proxy.config.exec_thread.watchdog.backlog_decrease_permille`), recovering proportionally
   once they are not (:ts:cv:`proxy.config.exec_thread.watchdog.backlog_increase_permille` of the depth
   in force, attenuated by the headroom remaining to the depth that began the episode), floored at
   :ts:cv:`proxy.config.exec_thread.watchdog.backlog_min_permille`.

   Held steady rather than moved in either direction while ``proxy.process.watchdog.max_awake_ms`` sits
   inside the release deadband - under the shrink threshold, but not yet down to
   :ts:cv:`proxy.config.exec_thread.watchdog.backlog_decrease_permille` of it.

   The SYNs the kernel drops as a result are invisible to |TS|, so this gauge and the listen-queue
   overflow counters (``ListenOverflows`` and ``ListenDrops`` from ``netstat -s`` or ``nstat``) are
   the only way to see shedding happening. Shed clients are not refused; they retransmit their SYN
   with backoff - see :ts:cv:`proxy.config.exec_thread.watchdog.mode`.

.. ts:stat:: global proxy.process.net.watchdog_listen_backlog integer

   The absolute accept queue depth currently in force on the non-exempt listen sockets.

   Set to :ts:cv:`proxy.config.net.listen_backlog` when the ports are opened, and updated on each
   resize. Published because the percentage alone is not enough to tell you the depth - when
   :ts:cv:`proxy.config.net.listen_backlog` is ``-1`` the baseline is the resolved system
   ``somaxconn``, which cannot be read from configuration.

   The exempt loopback companion listeners are never resized and are not reflected here; they always
   keep the full configured depth.

Comparing these against ``proxy.process.watchdog.max_awake_ms`` is the way to tell whether
backpressure is doing anything. If ``max_awake_ms`` stays high while ``listen_backlog_permille`` sits at
its floor, admission is already bounded as far as it will go and the remaining cost is inside |TS|.

Note that a high ``max_awake_ms`` with a *low* event count per loop -
:ts:stat:`proxy.process.eventloop.events.10s` against
:ts:stat:`proxy.process.eventloop.count.10s` - means a thread is stalled inside a single unit of
work rather than buried under many of them. No amount of shedding shortens a stall, so investigate
the blocking operation instead of enabling mode ``1``.
