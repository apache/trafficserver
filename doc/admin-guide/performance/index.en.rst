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

.. _performance-tuning:

Performance Tuning
******************

|ATS| in its default configuration should perform suitably for running the
included regression test suite, but will need special attention to both its own
configuration and the environment in which it runs to perform optimally for
production usage.

There are numerous options and strategies for tuning the performance of |TS|
and we attempt to document as many of them as possible in the sections below.
Because |TS| offers enough flexibility to be useful for many caching and
proxying scenarios, which tuning strategies will be most effective for any
given use case may differ, as well as the specific values for various
configuration options.

.. toctree::
   :maxdepth: 2

Before You Start
================

One of the most important aspects of any attempt to optimize the performance
of a |TS| installation is the ability to measure that installation's
performance; both prior to and after any changes are made. To that end, it is
strongly recommended that you establish some means to monitor and record a
variety of performance metrics: request and response speed, latency, and
throughput; memory and CPU utilization; and storage I/O operations.

Attempts to tune a system without being able to compare the impact of changes
made will at best result in haphazard, *feel good* results that may end up
having no real world impact on your customers' experiences, and at worst may
even result in lower performance than before you started. Additionally, in the
all too common situation of budget constraints, having proper measurements of
existing performance will greatly ease the process of focusing on those
individual components that, should they require hardware expenditures or larger
investments of employee time, have the highest potential gains relative to
their cost.

Building Traffic Server
=======================

While the default compilation settings for |TS| will produce a set of binaries
capable of serving most caching and proxying needs, there are some build
options worth considering in specific environments.

.. TODO::

   - any reasons why someone wouldn't want to just go with distro packages?
     (other than "distro doesn't package versions i want")
   - list relevant build options, impact each can potentially have

Hardware Tuning
===============

As with any other server software, efficient allocation of hardware resources
will have a significant impact on |TS| performance.

CPU Selection
-------------

|ATS| uses a hybrid event-driven engine and multi-threaded processing model for
handling incoming requests. As such, it is highly scalable and makes efficient
use of modern, multicore processor architectures.

.. TODO::

   any benchmarks showing relative req/s improvements between 1 core, 2 core,
   N core? diminishing rate of return? can't be totally linear, but maybe it
   doesn't realistically drop off within the currently available options (i.e.
   the curve holds up pretty well all the way through current four socket xeon
   8 core systems, so given a lack of monetary constraint, adding more cores
   is a surefire performance improvement (up to the bandwidth limits), or does
   it fall off earlier, or can any modern 4 core saturate a 10G network link
   given fast enough disks?)

Memory Allocation
-----------------

Though |TS| stores cached content within an on-disk host database, the entire
:ref:`cache-directory` is always maintained in memory during server operation.
Additionally, most operating systems will maintain disk caches within system
memory. It is also possible, and commonly advisable, to maintain an in-memory
cache of frequently accessed content.

The memory footprint of the |TS| process is largely fixed at the time of server
startup. Your |TS| systems will need at least enough memory to satisfy basic
operating system requirements, as well as capacity for the cache directory, and
any memory cache you wish to use. The default settings allocate roughly 10
megabytes of RAM cache for every gigabyte of disk cache storage, though this
setting can be adjusted manually in :file:`records.config` using the setting
:ts:cv:`proxy.config.cache.ram_cache.size`. |TS| will, under the default
configuration, adjust this automatically if your system does not have enough
physical memory to accomodate the aforementioned target.

Aside from the cost of physical memory, and necessary supporting hardware to
make use of large amounts of RAM, there is little downside to increasing the
memory allocation of your cache servers. You will see, however, no benefit from
sizing your memory allocation larger than the sum of your content (and index
overhead).

Disk Storage
------------

Except in cases where your entire cache may fit into system memory, your cache
nodes will eventually need to interact with their disks. While a more detailed
discussion of storage stratification is covered in `Cache Partitioning`_ below,
very briefly you may be able to realize gains in performance by separating
more frequently accessed content onto faster disks (PCIe SSDs, for instance)
while maintaining the bulk of your on-disk cache objects, which may not receive
the same high volume of requests, on lower-cost mechanical drives.



Operating System Tuning
========================

|ATS| is supported on a variety of operating systems, and as a result the tuning
strategies available at the OS level will vary depending upon your chosen
platform.

General Recommendations
-----------------------

TCP Keep Alive
~~~~~~~~~~~~~~

TCP Congestion Control Settings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ephemeral and Reserved Ports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Jumbo Frames
~~~~~~~~~~~~

.. TODO:: would they be useful/harmful/neutral for anything other than local forward/transparent proxies?

Linux
-----

FreeBSD
-------

OmniOS / illumos
----------------

Mac OS X
--------

Traffic Server Tuning
=====================

|TS| itself, of course, has many options you may want to consider adjusting to
achieve optimal performance in your environment. Many of these settings are
recorded in :file:`records.config` and may be adjusted with the
:option:`traffic_ctl config set` command line utility while the server is operating.

CPU and Thread Optimization
---------------------------

Thread Scaling
~~~~~~~~~~~~~~

By default, |TS| creates 1.5 threads per CPU core on the host system. This may
be adjusted with the following settings in :file:`records.config`:

* :ts:cv:`proxy.config.exec_thread.autoconfig`
* :ts:cv:`proxy.config.exec_thread.autoconfig.scale`
* :ts:cv:`proxy.config.exec_thread.limit`

Thread Affinity
~~~~~~~~~~~~~~~

On multi-socket servers, such as Intel architectures with NUMA, you can adjust
the thread affinity configuration to take advantage of cache pipelines and
faster memory access, as well as preventing possibly costly thread migrations
across sockets. This is adjusted with :ts:cv:`proxy.config.exec_thread.affinity`
in :file:`records.config`. ::

    CONFIG proxy.config.exec_thread.affinity INT 1

Thread Stack Size
~~~~~~~~~~~~~~~~~

:ts:cv:`proxy.config.thread.default.stacksize`

.. TODO::

   is there ever a need to fiddle with this, outside of possibly custom developed plugins?

Polling Timeout
~~~~~~~~~~~~~~~

If you are experiencing unusually or unacceptably high CPU utilization during
idle workloads, you may consider adjusting the polling timeout with
:ts:cv:`proxy.config.net.poll_timeout`::

    CONFIG proxy.config.net.poll_timeout INT 60

Memory Optimization
-------------------

:ts:cv:`proxy.config.thread.default.stacksize`
:ts:cv:`proxy.config.cache.ram_cache.size`


Disk Storage Optimization
-------------------------

:ts:cv:`proxy.config.cache.force_sector_size`
:ts:cv:`proxy.config.cache.max_doc_size`
:ts:cv:`proxy.config.cache.target_fragment_size`

Cache Partitioning
~~~~~~~~~~~~~~~~~~

Network Tuning
--------------

:ts:cv:`proxy.config.net.connections_throttle`

Error responses from origins are conistent and costly
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If error responses are costly for your origin server to generate, you may elect
to have |TS| cache these responses for a period of time. The default behavior is
to consider all of these responses to be uncacheable, which will lead to every
client request to result in an origin request.

This behavior is controlled by both enabling the feature via
:ts:cv:`proxy.config.http.negative_caching_enabled` and setting the cache time
(in seconds) with :ts:cv:`proxy.config.http.negative_caching_lifetime`. ::

    CONFIG proxy.config.http.negative_caching_enabled INT 1
    CONFIG proxy.config.http.negative_caching_lifetime INT 10

SSL-Specific Options
~~~~~~~~~~~~~~~~~~~~

:ts:cv:`proxy.config.ssl.max_record_size`
:ts:cv:`proxy.config.ssl.session_cache`
:ts:cv:`proxy.config.ssl.session_cache.size`

Thread Types
------------

Logging Configuration
---------------------

.. TODO::

   binary vs. ascii output
   multiple log formats (netscape+squid+custom vs. just custom)
   overhead to log collation
   using direct writes vs. syslog target

Plugin Tuning
=============

Common Scenarios and Pitfalls
=============================

While environments vary widely and |TS| is useful in a great number of different
situations, there are at least some recurring elements that may be used as
shortcuts to identifying problem areas, or realizing easier performance gains.

.. TODO::

   - origins not sending proper expiration headers (can fix at the origin (preferable) or use proxy.config.http.cache.heuristic_(min|max)_lifetime as hacky bandaids)
   - cookies and http_auth prevent caching
   - avoid thundering herd with read-while-writer (link to section in http-proxy-caching)
