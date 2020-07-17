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


This plugin listens for plugin msgs and invokes jemalloc control
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

Plugin Messages
===============

The plugin responds to the following mesages sent via traffic_ctl.

Message    Action
========== ===================================================================================
activate   Start jemalloc profiling. Useful if prof_active:false was in the configure string.

deactivate Stop jemalloc profiling.

dump       If profiling is enabled and active, it will generate a profile dump file.

stats      Print jemalloc statistics in traffic.out

The command below sends the stats message to the plugin causing the current statistics to be written to traffic.out::

    traffic_ctl plugin msg memory_profile stats

Limitations
===========

Currently the plugin only functions for systems compiled against jemalloc.
Perhaps in the future, it can be augmented to interact with other memory
allocation systems.

