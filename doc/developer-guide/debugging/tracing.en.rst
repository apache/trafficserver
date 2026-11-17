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

Dispatch tracepoints
====================

These tracepoints bracket the places where the event loop calls a handler. They name the handler that a thread is
running, so a thread that does not return to its event loop can be traced to the handler that holds it. Each
``handler`` argument is the address of a function whose symbol names the handler member function, so
``usym(handler)`` in bpftrace gives its name. The end tracepoints repeat the begin arguments: the handler can free the
continuation, so do not read memory through ``cont`` after it returns.

``event_dispatch_begin(EThread *thread, Continuation *cont, void *handler, int event)`` and ``event_dispatch_end`` with the same arguments
   The thread calls the handler of a scheduled event.

``vio_dispatch_begin(UnixNetVConnection *vc, Continuation *cont, void *handler, int event, int op)`` and ``vio_dispatch_end`` with the same arguments
   The thread signals the continuation of a read (``op`` 1) or write (``op`` 2) VIO on ``vc``.

``plugin_dispatch_begin(TSCont contp, TSEventFunc func, int event, void *edata)`` and ``plugin_dispatch_end(TSCont contp, TSEventFunc func, int event, int result)``
   The thread calls the event function of a plugin continuation. ``func`` is the plugin's own function.

These tracepoints fire for each dispatched event, so attach them only for short periods on a busy server.
