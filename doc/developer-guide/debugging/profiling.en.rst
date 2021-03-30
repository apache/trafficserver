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

.. _developer-profiling:

Profiling
*********

There are two main options for performance profiling: perf and gperf.

perf
====

The perf top option is useful to quickly identify functions that are taking a larger than expected
portion of the execution time.::

  sudo perf top -p `pidof traffic_server`

For more details use the record subcommand to gather profiling data on the traffic_server process.  Using
the -g option will gather call stack information.  Compiling with -ggdb and -fno-omit-frame-pointer
will make it more likely that perf record will gather complete callstacks.::

  sudo perf record -g -p `pidof traffic_server`

After gathering profilng data with perf record, use perf report to display the call stacks with their corresponding
contribution to total execution time.::

  sudo perf report

gperf
=====

Gperftools also provides libraries to statistically sample the callstacks of a process.  The --with-profile=yes option for configure will
link with the gperftools profiling library and add profile stop and profile dump function calls at the beginning and end of the traffic_server
main function.  The profilng data will be dumped in /tmp/ts.prof.

Once the profiling data file is present, you can use the pprof tool to generate a pdf callgraph of the data to see which
call stacks contribute most to the execution time.::

  pprof --pdf /opt/trafficserver/9.0/bin/traffic_server ts.prof > prof.pdf

