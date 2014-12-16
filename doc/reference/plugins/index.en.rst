Plugin Reference
****************

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

Overview
========

One of the key features of Apache Traffic Server is modularity.
Features that aren't needed in the core simply aren't there. This
is a good thing, because it guarantees that our core can remain
fast by concentrating on the things that we always provide: Caching
and proxying.

All other things can be moved into plugins, by opening up a consistent
C API, everyone can implement their own functionality, without
having to touch the core.

Stable plugins
==============

Plugins that are considered stable are installed by default in
Apache Traffic Server releases.

.. toctree::
  :maxdepth: 1

  CacheURL Plugin: allows you to change the key that is used for caching a request by using any portion of the url via regex <cacheurl.en>
  conf_remap Plugin: allows you to override configuration directives dependent on actual remapping rules <conf_remap.en>
  gzip / deflate Plugin: gzips or deflates responses <gzip.en>
  Header Rewrite Plugin: allows you to modify various headers based on defined rules (operations) on a request or response <header_rewrite.en>
  Regex Remap Plugin: allows you to configure mapping rules based on regular expressions <regex_remap.en>
  Stats over HTTP Plugin: implements an HTTP interface to all Traffic Server statistics <stats_over_http.en>
  TCPInfo Plugin: logs TCP metrics at various points in the HTTP processing pipeline <tcpinfo.en>

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

  AuthProxy Plugin: delegates the authorization decision of a request to an external HTTP service <authproxy.en>
  Background Fetch Plugin: allows you to proactively fetch content from Origin in a way that it will fill the object into cache <background_fetch.en>
  Balancer Plugin: balances requests across multiple origin servers <balancer.en>
  Buffer Upload Plugin: buffers POST data before connecting to the Origin server <buffer_upload.en>
  Combohandler Plugin: provides an intelligent way to combine multiple URLs into a single URL, and have Apache Traffic Server combine the components into one response <combo_handler.en>
  Epic Plugin: emits Traffic Server metrics in a format that is consumed tby the Epic Network Monitoring System <epic.en>
  ESI Plugin: implements the ESI specification <esi.en>
  Generator Plugin: generate arbitrary response data <generator.en>
  GeoIP ACLs Plugin: denying (or allowing) requests based on the source IP geo-location <geoip_acl.en>
  hipes.en
  Metalink Plugin: implements the Metalink download description format in order to try not to download the same file twice. <metalink.en>
  MySQL Remap Plugin: allows dynamic “remaps” from a database <mysql_remap.en>
  AWS S3 Authentication plugin: provides support for the Amazon S3 authentication features <s3_auth.en>
  SSL Headers: Populate request headers with SSL session information <sslheaders.en>
  stale_while_revalidate.en
  ts-lua Plugin: allows plugins to be written in Lua instead of C code <ts_lua.en>
  XDebug Plugin: allows HTTP clients to debug the operation of the Traffic Server cache using the X-Debug header <xdebug.en>
