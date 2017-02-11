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

.. _traffic_cache_tool:

traffic_cache_tool
******************

Synopsis
========

:program:`traffic_cache_tool` [OPTIONS] SUBCOMMAND [OPTIONS]

.. _traffic-cache-tool-commands:

Description
===========

:program:`traffic_cache_tool` is designed to interact with the |TS| cache both for inspection and modification.

:program:`traffic_cache_tool alloc`
    Perform cache storage allocation operations.
:program:`traffic_cache_tool list`
   Display information about the cache.

Options
=======

.. program:: traffic_cache_tool

.. option:: --span

    Specify the span (storage) to operate one. This can be a device, a cache directory, or a configuration file in the format of :file:`storage.config`. In the latter case all devices listed in the configuration file become active.

.. option:: --volume

    Specify the volume configuration file in the format of :file:`volume.config`. This is important primarily for allocation operations where having the volume configuration is needed in order to properly allocate storage in spans to specific volumes.

.. option:: --write

   Enable writing to storage devices. If this flag is not present then no operation will write to any storage device. This makes "dry run" the default and actual changes require specifying this flag.

Subcommands
===========

traffic_cache_tool alloc
------------------------
.. program:: traffic_cache_tool alloc
.. option:: free

   Allocate space on all spans that are empty. Requires a volume confiuration file to be specified.

traffic_cache_tool list
-----------------------
.. program:: traffic_cache_tool list
.. option:: stripes

   Search the spans for stripe data and display it. This is potentially slow as large sections of the disk may need to be read to find the stripe headers.

Examples
========

List the basic span data.

    $ traffic_cache_tool list

See also
========

:manpage:`storage.config(5)`
:manpage:`volume.config(5)`,
