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

========
Synopsis
========

:program:`traffic_cache_tool` [OPTIONS] COMMAND [SUBCOMMAND ...] [OPTIONS]

.. _traffic-cache-tool-commands:

===========
Description
===========

:program:`traffic_cache_tool` is designed to interact with the |TS| cache both for inspection and
modification. It uses a nested command keyword style for specifying operations. There are some
global options that apply to all commands. These should be specified before any command. These can
be abbreviated to any unique initial substring (e.g. "--sp" for "--span").

.. program:: traffic_cache_tool

.. option:: --help

   Prints a brief usage message along with the current command hierarchy.

.. option:: --spans

    Specify the span (storage) configuration. This can be a device, a cache directory, or a
    configuration file in the formof :file:`storage.config`. In the latter case all devices listed
    in the configuration file become active.

.. option:: --volumes

    Specify the volume configuration file in the format of :file:`volume.config`. This is important
    primarily for allocation operations where having the volume configuration is needed in order to
    properly allocate storage in spans to specific volumes.

.. option:: --write

   Enable writing to storage devices. If this flag is not present then all storage will be opened
   read only and no operation will write to any storage device. This makes "dry run" the default and
   actual changes require specifying this flag.

.. option:: --aos

   Specific the average object size in bytes. This is used in various computations. It is identical
   to :ts:cv:`proxy.config.cache.min_average_object_size`.

.. option:: --input

    Specify the input file or disk.

===========
Commands
===========

``list``
   Search the spans for stripe data and display it. This is potentially slow as large sections of
   the disk may need to be read to find the stripe headers.

   ``stripes``
      Print internal stripe metadata.

``clear``
   Clear all the spans by writing updated span headers.

   ``span``
     Clears an specific span and it's stripes. The span to be cleared is specified via ``--device``

``dir_check``
   Perform diagnostics on the stripe directories.

   ``full``
      Full check of the directories.

   ``freelist``
      Validate the directory free lists.

   ``bucket_chain``
      Validate the bucket chains in the directories.

``volumes``
   Compute storage allocation to stripes based on the volume configuration and print it.

``alloc``
   Allocate storage to stripes, updating the span and stripe headers.

   ``free``
      Allocate only free (unused) storage to volumes, updating span and stripe headers as needed.

``init``
   Initializes an uninitialized span and creates stripes on that span according to the volume and storage configuration
   The span to be initialized can be passed via ``--input``

``find``
  Determines the stripe in disk cache where the content corresponding to the provided URL may be cached.
  This command takes an input file which lists all the urls for which the stripe assignment needs to be determined.

========
Examples
========

List the basic span data.::

    traffic_cache_tool --spans=/usr/local/etc/trafficserver/storage.config list

Allocate unused storage space.::

   traffic_cache_tool \
      --spans=/usr/local/etc/trafficserver/storage.config \
      --volumes=/usr/local/etc/trafficserver/volume.config \
      alloc free

Clear all spans.::

     traffic_cache_tool \
      --spans=/usr/local/etc/trafficserver/storage.config \
      --volumes=/usr/local/etc/trafficserver/volume.config \
      clear

Clear a single span.::

    traffic_cache_tool \
    --span /opt/etc/trafficserver/storage.config \
    --volume /opt/etc/trafficserver/volume.config \
    clear span --device "/dev/sdb3" --write

Initialize a new span.::

    traffic_cache_tool \
    --span /opt/etc/trafficserver/storage.config \
    --volume /opt/etc/trafficserver/volume.config \
    init --input "/dev/sdb3" --write

Find Stripe Assignment.::

    traffic_cache_tool \
    --span /opt/etc/trafficserver/storage.config \
    --volume /opt/etc/trafficserver/volume.config \
    init --input "/home/user/urls.txt"

========
See also
========

:manpage:`storage.config(5)`
:manpage:`volume.config(5)`,
