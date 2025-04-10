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
