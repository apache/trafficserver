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

How It Works
============

The `slice` plugin takes GET requests and breaks them into
successive aligned range requested blocks.  It issues these
range requests back into the ATS instance and relies on the
`cache_range_requests` plugin to interact with the caching layer.

This design was chosen because the `cache_range_requests`
plugin was already proven to work well with the ATS caching layer.
The `slice` plugin has the already difficult task of managing
multiple block requests and all of the associated flow control
between the cache_range_requests plugin and the downstream clients.

Configuration
=============

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

The slice plugin supports the following options::

    --blockbytes=<bytes> (optional)
        Default is 1m or 1048576 bytes
        -b <bytes> for short.
        Suffix k,m,g supported
        Limited to 32k and 128m inclusive.

    --blockbytes-test=<bytes> (optional)
        Suffix k,m,g supported
        -t <bytes> for short.
        Limited to any positive number.
        Ignored if --blockbytes provided.

    --disable-errorlog (optional)
        Disable writing block stitch errors to the error log.
        -d for short

    --exclude-regex=<regex> (optional)
        If provided, only slice what matches.
        If not provided will always slice
        Cannot be used with --include-regex
        -e for short

    --include-regex=<regex> (optional)
        If provided, only slice what matches.
        If not provided will always slice
        Cannot be used with --exclude-regex
        -i for short

    --pace-errorlog=<seconds> (optional)
        Limit stitching error logs to every 'n' second(s)
        -p for short

    --ref-relative (optional)
        Self healing mode typically uses slice 0 as the reference slice
        for every request.  This is very safe but also increases plugin
        time and latency as the first slice is always fully processed
        whether or not the original requests needs any data from slice 0.
        This option uses the first slice in the request as reference
        which has better performance.  A downside of this mode is that
        self healing won't happen if blocks in a request agree.
        Normally leave this off.
        -l for short

    --remap-host=<loopback hostname> (optional)
        Uses effective url with given hostname for remapping.
        Requires setting up an intermediate loopback remap rule.
        -r for short

    --skip-header=<header name> (default: X-Slicer-Info)
        Header name used by the slice plugin after the loopback
        to indicate that the slice plugin should be skipped.
        -s for short

    --crr-ims-header=<header name> (default: X-Crr-Ims)
        Header name used by the slice plugin to tell the
        `cache_range_requests` plugin that a request should
        be marked as STALE.  Used for self healing.
        This must match the `--ims-header` option used by the
        `cache_range_requests` plugin.
        -i for short

    --crr-ident-header=<header name> (default: X-Crr-Ident)
        Header name used by the slice plugin to tell the
        `cache_range_requests` plugin the identifier of the
        first/reference slice fetched.  First Etag is preferred
        followed by Last-Modified. The `cache_range_requests`
        can use this identifier to flip a STALE asset back to
        FRESH in order to limit unnecessary IMS requests.

    --prefetch-count=<int> (optional)
        Default is 0
        Prefetches successive 'n' slice block requests in the background
        and caches (with `cache_range_requests` plugin). Prefetching is only
        enabled when first block (of the client request) is a cacheable object
        with miss or hit-stale status. Especially for large objects, prefetching
        can improve cache miss latency.
        -f for short

    --strip-range-for-head (optional)
        Enable slice plugin to strip Range header for HEAD requests.
        -h for short

    --minimum-size (optional)
    --metadata-cache-size (optional)
    --stats-prefix (optional)
        In combination, these three options allow for conditional slice.
        Specify the minimum size object to slice with --minimum-size.  Allowed
        values are the same as --blockbytes.  Conditional slicing uses a cache
        of object sizes to make the decision of whether to slice.  The cache
        will only store the URL of large objects as they are discovered in
        origin responses.  You should set the --metadata-cache-size to by
        estimating the working set size of large objects.  You can use
        stats to determine whether --metadata-cache-size was set optimally.
        Stat names are prefixed with the value of --stats-prefix.  The names
        are:

        <prefix>.metadata_cache.true_large_objects - large object cache hits
        <prefix>.metadata_cache.true_small_objects - small object cache hits
        <prefix>.metadata_cache.false_large_objects - large object cache misses
        <prefix>.metadata_cache.false_small_objects - small object cache misses
        <prefix>.metadata_cache.no_content_length - number of responses without content length
        <prefix>.metadata_cache.bad_content_length - number of responses with invalid content length
        <prefix>.metadata_cache.no_url - number of responses where URL parsing failed

        If an object size is not found in the object size cache, the plugin
        will not slice the object, and will turn off ATS cache on this request.
        The object size will be cached in following requests, and slice will
        proceed normally if the object meets the minimum size requirement.

        Range requests from the client for small objects are passed through the
        plugin unchanged.  If you use the `cache_range_requests` plugin, slice plugin
        will communicate with `cache_range_requests` using an internal header
        that causes `cache_range_requests` to be bypassed in such requests, and
        allow ATS to handle those range requests internally.



Examples::

    @plugin=slice.so @pparam=--blockbytes=1000000 @plugin=cache_range_requests.so

Or alternatively::

    @plugin=slice.so @pparam=-b @pparam=1000000 @plugin=cache_range_requests.so

Byte suffix examples::

    slice.so --blockbytes=5m
    slice.so -b 512k
    slice.so --blockbytes=32m

For testing and extreme purposes the parameter ``blockbytes-test`` may
be used instead which is unchecked::

    slice.so --blockbytes-test=1G
    slice.so -t 13

Because the slice plugin is susceptible to errors during block stitching
extra logs related to stitching are written to ``diags.log``.  Worst case
an error log entry could be generated for every transaction.  The
following options are provided to help with log overrun::

    slice.so --pace-errorlog=5
    slice.so -p 1
    slice.so --disable-errorlog

After modifying :file:`remap.config`, restart or reload |TS|
(sudo traffic_ctl config reload) or (sudo traffic_ctl server restart)
to activate the new configuration values.

Don't slice txt files::

  slice.so --exclude-regex=\\.txt
  slice.so -e \\.txt

Slice only mp4 files::

  slice.so --include-regex=\\.mp4
  slice.so -i \\.mp4

Debug Options
-------------

While the current slice plugin is able to detect block consistency
errors during the block stitching process, it can only abort the
client connection.  A CDN can only "fix" these by issuing an appropriate
content revalidation.

Under normal logging these slice block errors tend to show up as::

    pscl value 0
    crc value ERR_READ_ERROR

By default more detailed stitching errors are written to ``diags.log``.

.. topic:: Example

    ERROR: [slice.cc: 288] logSliceError(): 1555705573.639 reason="Non 206 internal block response" uri="http://ats_ep/someasset.mp4" uas="curl" req_range="bytes=1000000-" norm_range="bytes 1000000-52428799/52428800" etag_exp="%221603934496%22" lm_exp="Fri, 19 Apr 2019 18:53:20 GMT" blk_range="21000000-21999999" status_got="206" cr_got="" etag_got="%221603934496%22" lm_got="" cc="no-store" via=""

    ERROR: [server.cc: 288] logSliceError(): 1572370000.219 reason="Mismatch block Etag" uri="http://ats_ep/someasset.mp4" uas="curl" req_range="bytes=1092779033-1096299354" norm_range="bytes 1092779033-1096299354/2147483648" etag_exp="%223719843648%22" lm_exp="Tue, 29 Oct 2019 14:40:00 GMT" blk_range="1095000000-1095999999" status_got="206" cr_got="bytes 1095000000-1095999999/2147483648" etag_got="%223719853648%22" lm_got="Tue, 29 Oct 2019 17:26:40 GMT" cc="max-age=10000" via=""

Whether or how often these detailed log entries are written are
configurable plugin options.

Implementation Notes
====================

This slice plugin is a stop gap plugin for handling special cases
involving very large assets that may be range requested. Hopefully
the slice plugin is deprecated in the future when partial object
caching is finally implemented.

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

For each of these blocks separate sequential TSHttpConnect(s) are
made back into the front end of ATS.  By default of the remap plugins
are rerun.  Slice skips the remap due to presence of the X-Slicer-Info
header and allows cache_range_requests.so to serve the slice block back
to Slice either via cache OR parent request.

Slice assembles a header based on the very first slice block response
and sends it to the client.  If necessary it then skips over bytes in the
first block and starts sending byte content, examining each block header
and sends its bytes to the client until the client request is satisfied.

Any extra bytes at the end of the last block are consumed by the
Slice plugin to allow cache_range_requests to finish the block fetch to
ensure the block is cached.

Self Healing
------------

The slice plugin uses the very first slice as a reference slice which
uses content-length and last-modified and/or etags to ensure assembled
blocks come from the same asset.  In the case where a slice from a parent
is fetched which indicates that the asset has changed, the slice plugin
will attempt to self heal the asset.  The `cache_range_requests` plugin
must be configured with the `--consider-ims` parameter in order for
this to work.

Example `remap.config` configuration::

  map http://slice/ http://parent/ @plugin=slice.so @pparam=--remap-host=cache_range_requests
  map http://cache_range_requests/ http://parent/ @plugin=cache_range_requests.so @pparam=--consider-ims

When a request is served, the slice plugin uses reference slice 0 to
build a response to the client.  When subsequent slices are fetched they
are checked against this reference slice.  If a mismatch occurs an IMS
request for the offending slice is made through the `cache_range_requests`
plugin using an X-Crr-Ims header.  If the refetched slice still mismatches
then the client connection is aborted a crr IMS request is made for
the reference slice in an attempt to refetch it.

Optionally (but not recommended) the plugin may be configured to use
the first slice in the request as the reference slice.  This option
is faster since it does not visit any slices outside those needed to
fulfill a request.  However this may still cause problems if the
requested range was calculated from a newer version of the asset.

Purge Requests
--------------

The slice plugin supports PURGE requests, discarding the requested object from cache.
If a range is given in the client request, only the slice blocks from the
requested range will be purged (if in cache). If not, all of the blocks will be discarded
from the cache.

If a block receives a 404, indicating the requested block to be purged is not in the cache,
slice will not continue to purge the following blocks.

The functionality works with `--ref-relative` both enabled and disabled. If `--ref-relative` is
disabled (using slice 0 as the reference block), requesting to PURGE a block that does not have
slice 0 in its range will still PURGE the slice 0 block, as the reference block is always processed.

Conditional Slicing
-------------------

The goal of conditional slicing is to slice large objects and avoid the cost of slicing on small
objects.  If `--minimum-size` is specified, conditional slicing is enabled and works as follows.

The plugin builds a object size cache in memory.  The key is the URL of the object.  Only
large object URLs are written to the cache.  The object size cache uses CLOCK eviction algorithm
in order to have lazy promotion behavior.

When a URL not found in the object size cache, the plugin treats the object as a small object.  It
will not intercept the request.  The request is processed by ATS without any slice logic.  Upon
receiving a response, the slice plugin will check the response content length to update the object
size cache if necessary.

When a large URL is requested for the first time, conditional slicing will not intercept that
request since the URL is not known to be large.  This will cause an ATS cache miss and the request
will go to origin server.  Slice plugin will turn off writing to cache for this response, because
it expects to slice this object in future requests.

If the object size cache evicts a URL, the size of the object for that URL will need to be learned
again in a subsequent request, and the behavior above will happen again.

If the URL is found in the object size cache, conditional slicing treats the object as a large object
and will activate the slicing logic as described in the rest of this document.

If the client sends a range request, and that URL is not in the object size cache, the slice plugin
will forward the range request to ATS core.  It also attaches an internal header in order to deactivate
the `cache_range_requests` plugin for this range request.

Important Notes
===============

This plugin assumes that the content requested is cacheable.

Any first block server response that is not a 206 is passed directly
down to the client. Any 200 responses are passed back through to
the client.

Only the first server response block is used to evaluate any "If-"
conditional headers.  Subsequent server slice block requests
remove these headers.

The only 416 response that this plugin handles itself is if the
requested range is inside the last slice block but past the end of
the asset contents.  Other 416 responses are handled by the parent.

If a client aborts mid transaction the current slice block continues to
be read from the server until it is complete to ensure that the block
is cached.

Slice *always* makes ``blockbytes`` sized requests which are handled
by cache_range_requests.  The parent will trim those requests to
account for the asset Content-Length so only the appropriate number
of bytes are actually transferred and cached.

Effective URL remap
===================

By default the plugin restores the Pristine Url which reuses the same
remap rule for each slice block.  This is wasteful in that it reruns
the previous remap rules, and those remap rules must be smart enough to
check for the existence of any headers they may have created the first
time they have were visited.

To get around this the '--remap-host=<host>' or '-r <host>' option may
be used.  This requires an intermediate loopback remap to be defined which
handles each slice block request.

This works well with any remap rules that use the url_sig or uri_signing
plugins.  As the client remap rule is not caching any plugins that
manipulate the cache key would need to go into the loopback to parent
remap rule.

NOTE: Requests NOT handled by the slice plugin (ie: HEAD requests) are
handled as with a typical remap rule.  GET requests intercepted by the
slice plugin are virtually reissued into ATS and are proxied through
another remap rule which must contain the ``cache_range_requests`` plugin

Examples::

    map http://ats/ http://parent/ @plugin=slice.so @pparam=--remap-host=loopback
    map http://loopback/ http://parent/ @plugin=cache_range_requests.so

Alternatively::

    map http://ats/ http://parent/ @plugin=slice.so @pparam=-r @pparam=loopback
    map http://loopback/ http://parent/ @plugin=cache_range_requests.so

Current Limitations
===================

Since the Slice plugin is written as an intercept handler it loses the
ability to use normal state machine hooks and transaction states. This
functionality is handled by using the ``cache_range_requests`` plugin
to interact with ATS.
