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

.. _admin-plugins-collapsed-forwarding:

Collapsed Forwarding Plugin
***************************

This is a plugin for Apache Traffic Server that allows to achieve
effective connection collapse by blocking all but one of the multiple
concurrent requests for the same object from going to the Origin.

Installation
------------

To make this plugin available, you must either enable experimental plugins
when building |TS|::

    ./configure --enable-experimental-plugins

Or use :program:`tsxs` to compile the plugin against your current |TS| build.
To do this, you must ensure that:

#. Development packages for |TS| are installed.

#. The :program:`tsxs` binary is in your path.

#. The version of this plugin you are building, and the version of |TS| against
   which you are building it are compatible.

Once those conditions are satisfied, enter the source directory for the plugin
and perform the following::

    make -f Makefile.tsxs
    make -f Makefile.tsxs install

Using the plugin
----------------

This plugin can function as a per remap plugin or a global plugin, and it takes two optional
arguments for specifying the delay between successive retries and a max
number of retries.

To activate the plugin in per remap mode, in :file:`remap.config`, simply append the
below to the specific remap line::

  @plugin=collapsed_forwarding.so @pparam=--delay=<delay> @pparam=--retries=<retries>

To activate the plugin globally, in :file:`plugin.config`, add the following line::

  collapsed_forwarding.so --delay=<delay> --retries=<retries>

If the plugin is enabled both globally and per remap, Traffic Server will issue an error on startup.

Functionality
-------------

Traffic Server plugin to allow collapsed forwarding of concurrent requests for
the same object. This plugin is based on open_write_fail_action feature, which
detects cache open write failure on a cache miss and returns a 502 error along
with a special @-header indicating the reason for 502 error. The plugin acts
on the error by using an internal redirect follow back to itself, essentially
blocking the request until a response arrives, at which point, relies on
read-while-writer feature to start downloading the object to all waiting
clients. The following config parameters are assumed to be set for this
plugin to work:

- :ts:cv:`proxy.config.http.cache.open_write_fail_action`        ``1``
- :ts:cv:`proxy.config.cache.enable_read_while_writer`           ``1``
- :ts:cv:`proxy.config.http.number_of_redirections`             ``10``
- :ts:cv:`proxy.config.http.redirect_use_orig_cache_key`         ``1``
- :ts:cv:`proxy.config.http.background_fill_active_timeout`      ``0``
- :ts:cv:`proxy.config.http.background_fill_completed_threshold` ``0``

Additionally, given that collapsed forwarding works based on cache write
lock failure detection, the plugin requires cache to be enabled and ready.
On a restart, Traffic Server typically takes a few seconds to initialize
the cache depending on the cache size and number of dirents. While the
cache is not ready yet, collapsed forwarding can not detect the write lock
contention and so can not work. The setting :ts:cv:`proxy.config.http.wait_for_cache`
may be enabled which allows blocking incoming connections from being
accepted until cache is ready.

Description
-----------
Traffic Server has been affected severely by the Thundering Herd problem caused
by its inability to do effective connection collapse of multiple concurrent
requests for the same segment. This is especially critical when Traffic Server
is used as a solution to use cases such as delivering a large scale video
live streaming. This problem results in a specific behavior where multiple
number of requests for the same file are leaked upstream to the Origin layer
choking the upstream bandwidth due to the duplicated large file downloads or
process intensive file at the Origin layer. This ultimately can cause
stability problems on the origin layer disrupting the overall network
performance.

Traffic Server supports several kind of connection collapse mechanisms including
Read-While-Writer (RWW), Stale-While-Revalidate (SWR) etc each very effective
dealing with a majority of the use cases that can result in the
Thundering herd problem.

For a large scale Video Streaming scenario, there's a combination of a
large number of revalidations (e.g. media playlists) and cache misses
(e.g. media segments) that occur for the same file. Traffic Server's
RWW works great in collapsing the concurrent requests in such a scenario,
however, as described in :ref:`admin-configuration-reducing-origin-requests`,
Traffic Server's implementation of RWW has a significant limitation, which
restricts its ability to invoke RWW only when the response headers are
already received. This means that any number of concurrent requests for
the same file that are received before the response headers arrive are
leaked upstream, which can result in a severe Thundering herd problem,
depending on the network latencies (which impact the TTFB for the
response headers) at a given instant of time.

To address this limitation, Traffic Server supports a few Cache tuning
solutions, such as Open Read Retry, and a new feature called
Open Write Fail action from 6.0. To understand how these approaches work,
it is important to understand the high level flow of how Traffic Server
handles a GET request.

On receiving a HTTP GET request, Traffic Server generates the cache key
(basically, a hash of the request URL) and looks up for the directory
entry (dirent) using the generated index. On a cache miss, the lookup
fails and Traffic Server then tries to just get a write lock for the
cache object and proceeds to the origin to download the object. On
the Other hand, if the lookup is successful, meaning, the dirent
exists for the generated cache key, Traffic Server tries to obtain
a read lock on the cache object to be able to serve it from the cache.
If the read lock is not successful (possibly, due to the fact that
the object's being written to at that same instant and the response
headers are not in the cache yet), Traffic Server then moves to the
next step of trying to obtain an exclusive write lock. If the write
lock is already held exclusively by another request (transaction), the
attempt fails and at this point Traffic Server simply disables the
cache on that transaction and downloads the object in a proxy-only
mode::

  1). Cache Lookup (lookup for the dirent using the request URL as cache key).
    1.1). If lookup fails (cache miss), goto (3).
    1.2). If lookup succeeds, try to obtain a read lock, goto (2).
  2). Open Cache Read (try to obtain read lock)
    2.1). If read lock succeeds, serve from cache, goto (4).
    2.2). If read lock fails, goto (3).
  3). Open Cache Write (try to obtain write lock).
    3.1). If write lock succeeds, download the object into cache and to the client in parallel
    3.2). If write lock fails, disable cache, and download to the client in a proxy-only mode.
  4). Done

As can be seen above, if a majority of concurrent requests arrive before
response headers are received, they hit (2.2) and (3.2) above. Open Read
Retry can help to repeat (2) after a configured delay on 2.2, thereby
increasing the chances for obtaining a read lock and being able to serve
from the cache.

However, the Open Read Retry can not help with the concurrent requests
that hit (1.1) above, jumping to (3) directly. Only one such request will
be able to obtain the exclusive write lock and all other requests are
leaked upstream. This is where, the recently developed Traffic Server
feature Open Write Fail Action will help. The feature detects the write
lock failure and can return a stale copy for a Cache Revalidation or a
5xx status code for a Cache Miss with a special internal header
<@Ats-Internal> that allows a TS plugin to take other special actions
depending on the use-case.

``collapsed_forwarding`` plugin catches that error in SEND_RESPONSE_HDR_HOOK
and performs an internal 3xx Redirect back to the same host, the configured
number of times with the configured amount of delay between consecutive
retries, allowing to be able to initiate RWW, whenever the response headers
are received for the request that was allowed to go to the Origin.


More details are available at :ref:`admin-configuration-reducing-origin-requests`