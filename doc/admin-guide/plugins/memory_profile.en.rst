.. include:: ../../common.defs

Memory_profile Plugin
*********************

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


This plugin listens for plugin messages and invokes jemalloc control
operations.

Installation
============

Add the following line to :file:`plugin.config`::

    memory_profile.so

In addition, |TS| must be able to read jemalloc configuration
information either through the ``JEMALLOC_CONF`` environment variable
or via the string sym linked to ``/etc/malloc.conf``.

For example, if the string below is in ``JEMALLOC_CONF`` or in the sym link string, it
enables profiling and indicates that the memory dump prefix is ``/tmp/jeprof``.::

    prof:true,prof_prefix:/tmp/jeprof

Details on configuration jemalloc options at `<http://jemalloc.net/jemalloc.3.html>`.
Changes to the configuration in ``JEMALLOC_CONF`` or ``/etc/malloc.conf`` require a process
restart to pick up.

Plugin Messages
===============

The plugin responds to the following messages sent via traffic_ctl.

Message    Action
========== ===================================================================================
activate   Start jemalloc profiling. Useful if prof_active:false was in the configure string.

deactivate Stop jemalloc profiling.

dump       If profiling is enabled and active, it will generate a profile dump file.

stats      Print jemalloc statistics in traffic.out

The command below sends the stats message to the plugin causing the current statistics to be written to traffic.out::

    traffic_ctl plugin msg memory_profile stats

Example Usage
=============

If your run time configuration string is::

    prof:true,prof_prefix:/tmp/jeprof:prof_active:false

|TS| has started without profile sampling started.  Perhaps you didn't want to profile the start up phase of |TS|.  To start
you need to send the activate message to the plugin::

    traffic_ctl plugin msg memory_profile activate

If your run time configuration string does not indicate that the profiling is not started (e.g. the prof_active field is missing or set to true), you do not
need to send the activate message.

After waiting sometime for |TS| to gather some memory allocation data, you can send the dump message::

    traffic_ctl plugin msg memory_profile dump

This will cause a file containing information about the current state of the |TS| memory allocation to be dumped in a file prefixed
by the value of prof_prefix.  In this example, it would be something like ``/tmp/jeprof.1234.0.m0.heap``, where 1234 is the process id
and 0 is a running counter indicating how many dumps have been performed on this process.  Each dump is independent of the others
and records the current stat of allocations since the profiling was activated.  The dump file can be processed by jeprof
to get text output or graphs. Details of how to use jeprof are in the man pages or `<https://manpages.debian.org/unstable/libjemalloc-dev/jeprof.1.en.html>`.

You may want to send the dump message periodically to analyze how the |TS| memory allocation changes over time.  This periodic dump can also be achieved by setting the
``lg_prof_interval`` option in the run time configuration string.

If the profiling is taking a significant amount of processing time and affecting |TS| performance, send the deactivate message to turn off profiling.::

    traffic_ctl plugin msg memory_profile deactivate

Send the stats message to cause detailed jemalloc stats to be printed in traffic.out.  These stats represent activity since the start of the |TS| process.::

    traffic_ctl plugin msg memory_profile stats

Limitations
===========

Currently the plugin only functions for systems compiled against jemalloc.
Perhaps in the future, it can be augmented to interact with other memory
allocation systems.

