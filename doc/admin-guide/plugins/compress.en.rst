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

.. _admin-plugins-compress:

Compress Plugin
***************

This plugin adds compression and decompression options to both origin and cache
responses.

Purpose
=======

Not all clients can handle compressed content. Not all origin servers are
configured to respond with compressed content when a client says it can accept
it. And it's not always necessary to make two separate requests to an origin,
and track two separate cache objects, for the same content - once for a
compressed version and another time for an uncompressed version.

This plugin tidies up these problems by transparently compressing or deflating
origin responses, as necessary, so that both variants of a response are stored
as :term:`alternates <alternate>` and the appropriate version is used for client responses,
depending on the client's indication (via an ``Accept`` request header) of what
it can support.

Additionally, this plugin adds configurability for what types of origin
responses will receive this treatment, which will be proxied and cached with
default behavior, and which may be explicitly disallowed to cache both
compressed and deflated versions (because, for example, the cost of compression
is known ahead of time to outweigh the space and bandwidth savings and you wish
to avoid |TS| even testing for the possibility).

Installation
============

This plugin is considered stable and is included with |TS| by default. There
are no special steps necessary for its installation.

Configuration
=============

This plugin can be used as either global plugin or remap plugin.
It can be enabled globally for |TS| by adding the following to your
:file:`plugin.config`::

   compress.so

With no further options, this will enable the following default behavior:

*  Enable caching of both compressed and uncompressed versions of origin
   responses as :term:`alternates <alternate>`.

*  Compress objects with `text/*` content types for every origin.

*  Don't hide `Accept` encoding headers from origin servers (for an offloading
   reverse proxy).

*  No URLs are disallowed from compression.

*  Disable flush (flush compressed content to client).

Alternatively, a configuration may be specified (shown here using the sample
configuration provided with the plugin's source)::

   compress.so <path-to-plugin>/sample.compress.config

This can be used as remap plugin by pointing to config file in remap rule
:file:`remap.config`::

   @plugin=compress.so @pparam=sample.compress.config

The following sections detail the options you may specify in the plugin's
configuration file. Options may be used globally, or may be specified on a
per-site basis by preceding them with a `[<site>]` line, where `<site>` is the
client-facing domain for which the options should apply.

Per site configuration for remap plugin should be ignored.

cache
-----

When set to ``true``, causes |TS| to cache both the compressed and uncompressed
versions of the content as :term:`alternates <alternate>`. When set to
``false``, |TS| will cache only the compressed or decompressed variant returned
by the origin. Enabled by default.

compressible-content-type
-------------------------

Provides a wildcard to match against content types, determining which are to be
considered compressible. This defaults to ``text/*``. Takes one Content-Type
per line.

allow
--------

Provides a wildcard pattern which will be applied to request URLs. Any which
match the pattern will be considered compressible, and only deflated versions
of the objects will be cached and returned to clients. This may be useful for
objects which already have their own compression built-in, to avoid the expense
of multiple rounds of compression for trivial gains. If the regex is preceded by
``!`` (for example ``allow !*/nothere/*``), it disables the plugin from those machine URLs.

enabled
-------

When set to ``true`` (the default) permits objects to be compressed, and when ``false``
effectively disables the plugin in the current context.

flush
-----

Enables (``true``) or disables (``false``) flushing of compressed objects to
clients. This calls the compression algorithm's mechanism (Z_SYNC_FLUSH and for gzip
and BROTLI_OPERATION_FLUSH for brotli) to send compressed data early.

remove-accept-encoding
----------------------

When set to ``true`` this option causes the plugin to strip the request's
``Accept-Encoding`` header when contacting the origin server. Setting this option to ``false``
will leave the header intact if the client provided it.

*  To ease the load on the origins.

*  For when the proxy parses responses, and the resulting compression and
   decompression is wasteful.

supported-algorithms
----------------------

Provides the compression algorithms that are supported, a comma separate list
of values. This will allow |TS| to selectively support ``gzip``, ``deflate``,
and brotli (``br``) compression. The default is ``gzip``. Multiple algorithms can
be selected using ',' delimiter, for instance, ``supported-algorithms
deflate,gzip,br``. Note that this list must **not** contain any white-spaces!

Note that if :ts:cv:`proxy.config.http.normalize_ae` is ``1``, only gzip will
be considered, and if it is ``2``, only br or gzip will be considered.

Examples
========

To establish global defaults for all site requests passing through |TS|, while
overriding just a handful for requests to content at ``www.example.com``, you
might create a configuration with the following options::

   # Set some global options first
   cache true
   remove-accept-encoding false
   compressible-content-type text/*
   compressible-content-type application/json
   flush false

   # Now set a configuration for www.example.com
   [www.example.com]
   cache false
   remove-accept-encoding true
   allow !/notthis/*.js
   allow /this/*.js
   flush true

   # Allows brotli encoded response from origin but is not capable of brotli compression
   [brotli.allowed.com]
   enabled true
   compressible-content-type text/*
   compressible-content-type application/json
   flush true
   supported-algorithms gzip,deflate

   # Supports brotli compression
   [brotli.compress.com]
   enabled true
   compressible-content-type text/*
   compressible-content-type application/json
   flush true
   supported-algorithms br,gzip

   # This origin does it all
   [bar.example.com]
   enabled false

Assuming the above options are in a file at ``/etc/trafficserver/compress.config``
the plugin would be enabled for |TS| in :file:`plugin.config` as::

   compress.so /etc/trafficserver/compress.config

Alternatively, the compress plugin can be used as a remap plugin: ::

   map http://www.example.com http://origin.example.com \
      @plugin=compress.so @pparam=compress.config

   $ cat /etc/trafficserver/compress.config
   enabled true
   cache true
   compressible-content-type *xml
   supported-algorithms
