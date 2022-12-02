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
setting can be adjusted manually in :file:`records.yaml` using the setting
:ts:cv:`proxy.config.cache.ram_cache.size`. |TS| will, under the default
configuration, adjust this automatically if your system does not have enough
physical memory to accommodate the aforementioned target.

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

macOS
--------

Traffic Server Tuning
=====================

|TS| itself, of course, has many options you may want to consider adjusting to
achieve optimal performance in your environment. Many of these settings are
recorded in :file:`records.yaml` and may be adjusted with the
:option:`traffic_ctl config set` command line utility while the server is operating.

CPU and Thread Optimization
---------------------------

Thread Scaling
~~~~~~~~~~~~~~

By default, |TS| creates one thread per CPU core on the host system. This may
be adjusted with the following settings in :file:`records.yaml`:

* :ts:cv:`proxy.config.exec_thread.autoconfig`
* :ts:cv:`proxy.config.exec_thread.autoconfig.scale`
* :ts:cv:`proxy.config.exec_thread.limit`

Thread Affinity
~~~~~~~~~~~~~~~

On multi-socket servers, such as Intel architectures with NUMA, you can adjust
the thread affinity configuration to take advantage of cache pipelines and
faster memory access, as well as preventing possibly costly thread migrations
across sockets. This is adjusted with :ts:cv:`proxy.config.exec_thread.affinity`
in :file:`records.yaml`. ::

    CONFIG proxy.config.exec_thread.affinity INT 1

Thread Stack Size
~~~~~~~~~~~~~~~~~

:ts:cv:`proxy.config.thread.default.stacksize`

.. TODO::

   is there ever a need to fiddle with this, outside of possibly custom developed plugins?

.. _admin-performance-timeouts:

Timeout Settings
----------------

|TS| has a variety of timeout settings which may be modified to help tune the
performance of various proxy components. In general it is recommended to leave
the timeouts at their default values unless you have identified specific causes
for an adjustment.

Note that not all proxy configurations will be impacted by every timeout. For
instance, if you are not using any hierarchical caching then the parent proxy
timeouts will be irrelevant.

While all of the timeouts described below may be set globally for your |TS|
instance using :file:`records.yaml`, many of them are also overridable on a
per-transaction basis by plugins (including :ref:`admin-plugins-conf-remap`).
This allows the possibility for adjusting timeout value for individual subsets
of your cache.

For example, you may wish to be fairly lenient on activity timeouts for most of
your cache, leaving the default at a minute or two, but enforce a much stricter
timeout on a set of very small, incredibly heavily accessed objects for which
you can construct a ``map`` rule with the goal of reducing the chances that a
few bad actors (misconfigured or misbehaving clients) may generate too much
connection pressure on your cache. The trade off may be that some perfectly
innocent, but slow clients may have their connections terminated early. As with
all performance tuning efforts, your needs are likely to vary from others' and
should be carefully considered and closely monitored.

Default Inactivity Timeout
~~~~~~~~~~~~~~~~~~~~~~~~~~

The :ts:cv:`proxy.config.net.default_inactivity_timeout` setting is applied to
the HTTP state machine when no other inactivity timeouts have been applied. In
effect, it sets an upper limit, in seconds, on state machine inactivity.

In addition to the timeout itself, there is a related statistic:
:ts:stat:`proxy.process.net.default_inactivity_timeout_applied` which tracks
the number of times the default inactivity timeout was applied to transactions
(as opposed to a more specific timeout having been applied).

::

    CONFIG proxy.process.net.default_inactivity_timeout INT 86400

Accept Timeout
~~~~~~~~~~~~~~

The variable :ts:cv:`proxy.config.http.accept_no_activity_timeout` sets, in
seconds, the time after which |TS| will close incoming connections which remain
inactive (have not sent data). Lowering this timeout can ease pressure on the
proxy if misconfigured or misbehaving clients are opening a large number of
connections without submitting requests.

::

    CONFIG proxy.config.http.accept_no_activity_timeout INT 120

Background Fill Timeout
~~~~~~~~~~~~~~~~~~~~~~~

When :ref:`background fills <admin-config-read-while-writer>` are enabled,
:ts:cv:`proxy.config.http.background_fill_active_timeout` sets in seconds the
time after which |TS| will abort the fill attempt and close the origin
server connection that was being used. Setting this to zero disables the
timeout, but modifying the value and enforcing a timeout may help in
situations where your origin servers stall connections without closing.

::

    CONFIG proxy.config.http.background_fill_active_timeout INT 0

DNS Timeouts
~~~~~~~~~~~~

|TS| performs all DNS queries for origin servers through the HostDB subsystem.
Two settings affect the potential frequency and amount of time |TS| will spend
on these lookups. :ts:cv:`proxy.config.hostdb.timeout` is used to establish the
time-to-live, in minutes, for all DNS records and
:ts:cv:`proxy.config.hostdb.lookup_timeout` sets, in seconds, the timeout for
actual DNS queries.

Setting a higher ``timeout`` value will reduce the number of times |TS| needs
to perform DNS queries for origin servers, but may also prevent your |TS|
instance from updating its records to reflect external DNS record changes in a
timely manner (refer to :ts:cv:`proxy.config.hostdb.ttl_mode` for more
information on when this TTL value will actually be used).

::

    CONFIG proxy.config.hostdb.timeout INT 1440
    CONFIG proxy.config.hostdb.lookup_timeout INT 30

Keepalive Timeouts
~~~~~~~~~~~~~~~~~~

|TS| keepalive timeouts may be set both for maintaining a client connection for
subsequent requests, using
:ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_in`, as well as origin
server connections for subsequent object requests (when not servable from the
cache) using :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_out`.
Both are specified in seconds. Keep in mind that
``keep_alive_no_activity_timeout_out`` for origin server connections is
effectively an advisory maximum, as the origin server may have its own
keepalive timeout which (if set lower) will likely take precedence.

::

    CONFIG proxy.config.http.keep_alive_no_activity_timeout_in INT 120
    CONFIG proxy.config.http.keep_alive_no_activity_timeout_out INT 120

Origin Connection Timeouts
~~~~~~~~~~~~~~~~~~~~~~~~~~

Origin server connection timeouts are configured with :ts:cv:`proxy.config.http.connect_attempts_timeout`,
which is applied both to the initial connection as well as any retries attempted,
should an attempt timeout. The timeout applies from the moment |TS| begins the
connection attempt until the origin fully establishes a connection (the connection is ready to write).
After the connection is established the value of
:ts:cv:`proxy.config.http.transaction_no_activity_timeout_out` is used to established timeouts on the data over the connection.

::

    CONFIG proxy.config.http.connect_attempts_timeout INT 30

Polling Timeout
~~~~~~~~~~~~~~~

If you are experiencing unusually or unacceptably high CPU utilization during
idle workloads, you may consider adjusting the polling timeout with
:ts:cv:`proxy.config.net.poll_timeout`::

    CONFIG proxy.config.net.poll_timeout INT 60

SOCKS Timeouts
~~~~~~~~~~~~~~

In |TS| configurations where SOCKS has been enabled, three timeouts are made
available for tuning. Basic activity timeout for SOCKS server connections may
be adjusted with :ts:cv:`proxy.config.socks.socks_timeout`, in seconds. Server
connection attempts (initial connections attempts only) are covered by
:ts:cv:`proxy.config.socks.server_connect_timeout`, again in seconds, and
server connection retry attempts are set with
:ts:cv:`proxy.config.socks.server_retry_timeout`. Note that the retry timeout
is the timeout for the actual connection attempt on a retry, not the delay
after which a retry will be performed (the delay is configured with
:ts:cv:`proxy.config.socks.server_retry_time`).

::

    CONFIG proxy.config.socks.socks_timeout INT 100
    CONFIG proxy.config.socks.server_connect_timeout INT 10
    CONFIG proxy.config.socks.server_retry_timeout INT 300
    CONFIG proxy.config.socks.server_retry_time INT 300

SSL Timeouts
~~~~~~~~~~~~

|TS| offers a few timeouts specific to encrypted connections handled by the SSL
engine.

:ts:cv:`proxy.config.ssl.handshake_timeout_in` configures the time, in seconds,
after which incoming client connections will abort should the SSL handshake not
be completed. A value of ``0`` will disable the timeout.

When :ref:`admin-ocsp-stapling` is enabled in |TS|, you can configure two
separate timeouts; one for setting the length of time which cached OCSP results
will persist, specified in seconds using
:ts:cv:`proxy.config.ssl.ocsp.cache_timeout`, and the timeout for requests to
the remote OCSP responders, in seconds, with
:ts:cv:`proxy.config.ssl.ocsp.request_timeout`.

Lastly, you can control the number of seconds for which SSL sessions will be
cached in |TS| using :ts:cv:`proxy.config.ssl.session_cache.timeout`.

::

    CONFIG proxy.config.ssl.handshake_timeout_in INT 30
    CONFIG proxy.config.ssl.ocsp.cache_timeout INT 3600
    CONFIG proxy.config.ssl.ocsp.request_timeout INT 10
    CONFIG proxy.config.ssl.session_cache.timeout INT 0

Transaction Activity Timeouts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

|TS| specifies two sets of general transaction activity timeouts: a pair for
active transactions, and a pair for inactive connections (ones which are not
receiving or sending data during the timeout period). Each pair includes one
timeout for client connections (the ``_in`` variant) and another for origin
server transactions (``_out`` variants).

For active transactions,
:ts:cv:`proxy.config.http.transaction_active_timeout_in` and
:ts:cv:`proxy.config.http.transaction_active_timeout_out` set the maximum time,
in seconds, which |TS| will spend sending/receiving data with a client or
origin server, respectively. If the data transfer has not completed within the
time specified then the connection will be closed automatically. This may
result in the lack of a cache update, or partial data transmitted to a client.
:ts:cv:`proxy.config.http.transaction_active_timeout_out` is disabled (set to ``0``) by default.
:ts:cv:`proxy.config.http.transaction_active_timeout_in` is set to 900 seconds by default.

In general, it's unlikely you will want to enable either of these timeouts
globally, especially if your cache contains objects of varying sizes and deals
with clients which may support a range of speeds (and therefore take less or
more time to complete normal, healthy data exchanges). However, there may be
configurations in which small objects need to be exchanged in very short
periods and you wish your |TS| cache to enforce these time restrictions by
closing connections which exceed them.

The variables :ts:cv:`proxy.config.http.transaction_no_activity_timeout_in` and
:ts:cv:`proxy.config.http.transaction_no_activity_timeout_out` control the
maximum amount of time which |TS| will spend in a transaction which is stalled
and not transmitting data, for clients and origin servers respectively.

Unlike the active transaction timeouts, these two inactive transaction timeout
values prove somewhat more generally applicable.

::

    CONFIG proxy.config.http.transaction_active_timeout_in INT 900
    CONFIG proxy.config.http.transaction_active_timeout_out INT 0
    CONFIG proxy.config.http.transaction_no_activity_timeout_in INT 30
    CONFIG proxy.config.http.transaction_no_activity_timeout_out INT 30

WebSocket Timeouts
~~~~~~~~~~~~~~~~~~

|TS| provides two configurable timeouts for WebSocket connections. The setting
:ts:cv:`proxy.config.websocket.no_activity_timeout` will establish the maximum
length of time a stalled WebSocket connection will remain before |TS| closes
it. :ts:cv:`proxy.config.websocket.active_timeout` sets the maximum duration
for all WebSocket connections, regardless of their level of activity.

::

    CONFIG proxy.config.websocket.no_activity_timeout INT 600
    CONFIG proxy.config.websocket.active_timeout INT 3600

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

Error responses from origins are consistent and costly
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If error responses are costly for your origin server to generate, you may elect
to have |TS| cache these responses for a period of time. The default behavior is
to consider all of these responses to be uncacheable, which will lead to every
client request resulting in an origin request.

This behavior is controlled by both enabling the feature via
:ts:cv:`proxy.config.http.negative_caching_enabled` and setting the cache time
(in seconds) with :ts:cv:`proxy.config.http.negative_caching_lifetime`. Configured
status code for negative caching can be set with :ts:cv:`proxy.config.http.negative_caching_list`. ::

    CONFIG proxy.config.http.negative_caching_enabled INT 1
    CONFIG proxy.config.http.negative_caching_lifetime INT 10
    CONFIG proxy.config.http.negative_caching_list STRING 204 305 403 404 414 500 501 502 503 504

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
   using direct writes vs. syslog target

Plugin Tuning
=============

Common Scenarios and Pitfalls
=============================

While environments vary widely and |TS| is useful in a great number of different
situations, there are at least some recurring elements that may be used as
shortcuts to identifying problem areas, or realizing easier performance gains.

.. TODO::

   - origins not sending proper expiration headers (can fix at the origin (preferable) or use proxy.config.http.cache.heuristic_(min|max)_lifetime as hacky band-aids)
   - cookies and http_auth prevent caching
   - avoid thundering herd with read-while-writer (link to section in http-proxy-caching)
