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

.. _admin-plugins:

Plugins
*******

.. toctree::
   :maxdepth: 2

Overview
========

One of the key features of |ATS| is its modularity. Features that aren't needed
in the core simply aren't there. This helps to provide an additional guarantee
that our core can remain fast by concentrating on the things that we always
provide: caching and proxying.

All other functionality can be moved into plugins and by offering a consistent
C API, everyone can implement their own functionality, without having to touch
the core.

Stable plugins
==============

Plugins that are considered stable are installed by default in |TS| releases.

.. toctree::
  :maxdepth: 1

  CacheURL: allows you to change the key that is used for caching a request by using any portion of the url via regex <cacheurl.en>
  Configuration Remap: allows you to override configuration directives dependent on actual remapping rules <conf_remap.en>
  GZip: gzips or deflates responses <gzip.en>
  Header Rewrite: allows you to modify various headers based on defined rules (operations) on a request or response <header_rewrite.en>
  Health Checks: allows you to define health check links <healthchecks.en>
  Regex Remap: allows you to configure mapping rules based on regular expressions <regex_remap.en>
  Stats over HTTP: implements an HTTP interface to all Traffic Server statistics <stats_over_http.en>
  TCPInfo: logs TCP metrics at various points in the HTTP processing pipeline <tcpinfo.en>

Experimental plugins
====================

Plugins that are considered experimental are located in the
`plugins/experimental <https://git-wip-us.apache.org/repos/asf?p=trafficserver.git;a=tree;f=plugins/experimental;hb=HEAD>`_
directory of the Apache Traffic Server source tree. Experimental plugins can be compiled by passing the
`--enable-experimental-plugins` option to `configure`::

    $ autoconf -i
    $ ./configure --enable-experimental-plugins
    $ make

.. toctree::
  :maxdepth: 1

  AWS S3 Authentication: provides support for the Amazon S3 authentication features <s3_auth.en>
  AuthProxy: delegates the authorization decision of a request to an external HTTP service <authproxy.en>
  Background Fetch: allows you to proactively fetch content from Origin in a way that it will fill the object into cache <background_fetch.en>
  Balancer: balances requests across multiple origin servers <balancer.en>
  Buffer Upload: buffers POST data before connecting to the Origin server <buffer_upload.en>
  Cache Promotion: provides additional control over when an object should be allowed into the cache <cache_promote.en>
  Cachekey: allows some common cache key manipulations based on various HTTP request elements <cachekey.en> 
  Combo Handler: provides an intelligent way to combine multiple URLs into a single URL, and have Apache Traffic Server combine the components into one response <combo_handler.en>
  ESI: implements the ESI specification <esi.en>
  Epic: emits Traffic Server metrics in a format that is consumed tby the Epic Network Monitoring System <epic.en>
  Generator: generate arbitrary response data <generator.en>
  GeoIP ACLs: denying (or allowing) requests based on the source IP geo-location <geoip_acl.en>
  MP4: mp4 streaming media <mp4.en>
  Memcache: implements the memcache protocol for cache contents <memcache.en>
  Metalink: implements the Metalink download description format in order to try not to download the same file twice. <metalink.en>
  MySQL Remap: allows dynamic “remaps” from a database <mysql_remap.en>
  SSL Headers: Populate request headers with SSL session information <sslheaders.en>
  XDebug: allows HTTP clients to debug the operation of the Traffic Server cache using the X-Debug header <xdebug.en>
  hipes.en
  stale_while_revalidate.en
  ts-lua: allows plugins to be written in Lua instead of C code <ts_lua.en>
  Signed URLs: adds support for verifying URL signatures for incoming requests to either deny or redirect access <url_sig.en>
