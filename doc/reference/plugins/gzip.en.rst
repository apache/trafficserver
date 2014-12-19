.. _gzip-plugin:

gzip / deflate Plugin
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


This plugin gzips or deflates responses, whichever is applicable. It can
compress origin respones as well as cached responses. The plugin is built
and installed as part of the normal Apache Traffic Server installation
process.

Installation
============

Add the following line to :file:`plugin.config`::

    gzip.so

In this case, the plugin will use the default behaviour:

-  Enable caching
-  Compress text/\* for every origin
-  Don't hide accept encoding from origin servers (for an offloading
   reverse proxy)
-  No urls are disallowed from compression
-  Disable flush (flush gzipped content to client)

Configuration
=============

Alternatively, a configuration can also be specified::

    gzip.so <path-to-plugin>/sample.gzip.config

After modifying plugin.config, restart traffic server (sudo
traffic_line -L) the configuration is re-read when a management update
is given (sudo traffic_line -x)

Options
=======

Flags and options are:

``enabled``: (``true`` or ``false``) Enable or disable compression for a
host.

``remove-accept-encoding``: (``true`` or ``false``) Sets whether the
plugin should hide the accept encoding from origin servers:

-  To ease the load on the origins.
-  For when the proxy parses responses, and the resulting
   compression/decompression is wasteful.

``cache``: (``true`` or ``false``) When set, the plugin stores the
uncompressed and compressed response as alternates.

``compressible-content-type``: Wildcard pattern for matching
compressible content types.

``disallow``: Wildcard pattern for disabling compression on urls.

``flush``: (``true`` or ``false``) Enable or disable flushing of gzipped content.

Options can be set globally or on a per-site basis, as such::

    # Set some global options first
    cache true
    enabled true
    remove-accept-encoding false
    compressible-content-type text/*
    flush false

    # Now set a configuration for www.example.com
    [www.example.com]
    cache false
    remove-accept-encoding true
    disallow /notthis/*.js
    flush true

See example.gzip.config for example configurations.
