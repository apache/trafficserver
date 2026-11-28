.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs
.. default-domain:: cpp

.. _developer-tracing:

Tracing
*********

ATS includes statically defined tracepoints.  These are useful for debugging a running instance with minimal overhead.

USDT
====

A number of tools can be used to read USDT tracepoints.  Tools such as bpftrace, bcc, and perf use eBPF technology within the Linux kernel to provide tracing capabilities.

These tools can list available tracepoints, attach to a tracepoint, and some can process trace data in real time with eBPF programs.

Built-in tracepoints
====================

ATS includes a tracepoint at each HTTP state machine state.  The state machine id, sm_id, is included as an argument to help isolate each transaction.  These tracepoints
are named milestone_<state> and are located in the HttpSM.cc file.  See :func:`TSHttpTxnMilestoneGet` for a list of the states.

Example with bpftrace
=====================
.. code-block:: bash

   #!/usr/bin/env bpftrace

   BEGIN {
      @ = (uint64)0;
   }

   usdt:/opt/ats/bin/traffic_server:trafficserver:milestone_sm_start {
      $sm_id = arg0;
      if (@ == 0) {
         @ = $sm_id;
      }
      @start_time = nsecs;
   }

   usdt:/opt/ats/bin/traffic_server:trafficserver:milestone_* {
      $sm_id = arg0;
      if ($sm_id == @) {
         printf("%s %d\n", probe, nsecs - @start_time);
      }
   }

   usdt:/opt/ats/bin/traffic_server:trafficserver:milestone_sm_finish {
      $sm_id = arg0;
      if ($sm_id == @) {
         printf("End of state machine %d\n", $sm_id);
         exit();
      }
   }

Event loop tracepoints
======================

These tracepoints show whether an ``ET_NET`` thread returns to its event loop. A tool that runs on the event
thread gets the kernel thread ID from the probe context, for example the ``tid`` builtin in bpftrace. A thread
named ``[ET_NET <n>]`` has index *n*.

``eventloop_sleep(EThread *thread, int index, int64_t timeout_ns)``
   The thread is about to block in its poll. ``timeout_ns`` is the longest time that it waits.

``eventloop_wake(EThread *thread, int index, int64_t poll_ns, int ready)``
   The thread returned from its poll. ``poll_ns`` is the time that it was in the poll, and ``ready`` is the
   number of ready descriptors. The thread is awake until the next ``eventloop_sleep``.

``watchdog_thread_awake(size_t index, EThread *thread, int64_t awake_ns)``
   The watchdog found that thread ``index`` has been awake for ``awake_ns``, which is more than the watchdog
   timeout, :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms` or the deprecated
   :ts:cv:`proxy.config.exec_thread.watchdog.timeout_ms`. It fires on the watchdog thread, once for each long
   loop iteration. It fires only when the watchdog is enabled.

Each tracepoint fires at most once for each loop iteration.

This script prints the kernel thread ID of each thread that the watchdog reports:

.. code-block:: bash

   #!/usr/bin/env bpftrace

   usdt:/opt/ats/bin/traffic_server:trafficserver:eventloop_wake {
      @tid_of[arg0] = tid;
   }

   usdt:/opt/ats/bin/traffic_server:trafficserver:watchdog_thread_awake {
      printf("[ET_NET %d] tid %d awake %d ms\n", arg0, @tid_of[arg1], arg2 / 1000000);
   }
