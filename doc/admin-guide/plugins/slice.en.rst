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

.. _admin-plugins-slice:

Slice Plugin
***************

This plugin takes client requests and breaks them up into
successive aligned block requests.  This supports both
whole asset and single range requests.

Purpose
=======

This slice plugin, along with the `cache_range_requests`
plugin allows the following:

-  Fulfill arbitrary range requests by fetching a minimum
   number of cacheable aligned blocks to fulfill the request.
-  Breaks up very large assets into much smaller cache
   blocks that can be spread across multiple storage
   devices and within cache groups.

Configuration
============

This plugin is intended for use as a remap plugin and is
configured in :file:`remap.config`.

Or preferably per remap rule in :file:`remap.config`::

    map http://ats/ http://parent/ @plugin=slice.so \
        @plugin=cache_range_requests.so

In this case, the plugin will use the default behaviour:

-  Fulfill whole file or range requests by requesting cacheable
   block aligned ranges from the parent and assemble them
   into client responses, either 200 or 206 depending on the
   client request.
-  Default block size is 1mb (1048576 bytes).
-  This plugin depends on the cache_range_requests plugin
   to perform actual parent fetching and block caching
   and If-* conditional header evaluations.

Plugin Options
--------------

Slice block sizes can specified using the blockbytes parameter::

    @plugin=slice.so @pparam=blockbytes:1000000 @cache_range_requests.so

In adition to bytes, 'k', 'm' and 'g' suffixes may be used for
kilobytes, megabytes and gigabytes::

    @plugin=slice.so @pparam=blockbytes:5m @cache_range_requests.so
    @plugin=slice.so @pparam=blockbytes:512k @cache_range_requests.so
    @plugin=slice.so @pparam=blockbytes:32m @cache_range_requests.so

paramater ``blockbytes`` is checked to be between 32kb and 32mb
inclusive.

For testing and extreme purposes the parameter ``bytesover`` may
be used instead which is unchecked::

    @plugin=slice.so @pparam=bytesover:1G @cache_range_requests.so
    @plugin=slice.so @pparam=bytesover:13 @cache_range_requests.so

After modifying :file:`remap.config`, restart or reload traffic server
(sudo traffic_ctl config reload) or (sudo traffic_ctl server restart)
to activate the new configuration values.

Implementation Notes
====================

This slice plugin is by no means a best solution for adding
blocking support to ATS.

The slice plugin as is designed to provide a basic capability to block
requests for arbitrary range requests and for blocking large assets for
ease of caching.

Slice *ONLY* handles slicing up requests into blocks, it delegates
actual caching and fetching to the cache_range_requests.so plugin.

Plugin Function
---------------

Below is a quick functional outline of how a request is served
by a remap rule containing the Slice plugin with cache_range_requests:

For each client request that comes in all remap plugins are run up
until the slice plugin is hit.  If the slice plugin *can* be run (ie:
GET request) it will handle the request and STOP any further plugins
from executing.

At this point the request is sliced into 1 or more blocks by
adding in range request headers ("Range: bytes=").  A special
header X-Slicer-Info header is added and the pristine URL is
restored.

For each of these blocks separate sequential TSHttpConnect(s) are made
back into the front end of ATS and all of the remap plugins are rerun.
Slice skips the remap due to presense of the X-Slicer-Info header and
allows cache_range_requests.so to serve the slice block back to Slice
either via cache OR parent request.

Slice assembles a header based on the first slice block response and
sends it to the client.  If necessary it then skips over bytes in
the first block and starts sending byte content, examining each
block header and sends its bytes to the client until the client
request is satisfied.

Any extra bytes at the end of the last block are consumed by
the the Slice plugin to allow cache_range_requests to finish
the block fetch to ensure the block is cached.

Important Notes
===============

This plugin also assumes that the content requested is cacheable.

Any first block server response that is not a 206 is passed directly
down to the client.  If that response is a '200' only the first
portion of the response is passed back and the transaction is closed.

Only the first server response block is used to evaluate any "If-"
conditional headers.  Subsequent server slice block requests
remove these headers.

The only 416 response that this plugin handles itself is if the
requested range is inside the last slice block but past the end of
the asset contents.  Other 416 responses are handled by the parents.

If a client aborts mid transaction the current slice block continues to
be read from the server until it is complete to ensure that the block
is cached.

Slice *always* makes ``blockbytes`` sized requests which are handled
by cache_range_requests.  The parent will trim those requests to
account for the asset Content-Length so only the appropriate number
of bytes are actually transferred and cached.

Current Limitations
===================

By restoring the prisine Url the plugin as it works today reuses the
same remap rule for each slice block.  This is wasteful in that it reruns
the previous remap rules, and those remap rules must be smart enough to
check for the existence of any headers they may have created the
first time they have were visited.

Since the Slice plugin is written as an intercept handler it loses the
ability to use state machine hooks and transaction states.

