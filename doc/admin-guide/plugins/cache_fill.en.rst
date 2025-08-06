.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. _admin-plugins-cache-fill.so:
.. include:: /common.defs

Cache Fill Plugin
***********************

The speed of the response served from the cache depends on the cache speed and the client filling the object.
This dependency could significantly impact all the clients requesting the object.
This plugin tries to eliminate the dependence by making the original request spawn a background request to fill the cache.
The initial version of this plugin relays the initial request to the origin server instead of waiting for the background request to start filling the cache as there is no easier way to find the wait time.
This plugin doesn't provide any improvement for smaller objects but could also degrade the performance as two outgoing requests for every cache update.


Configuration
-------------
This plugin functions as either a global or per remap plugin, and it takes an optional argument for
specifying a config file with inclusion or exclusion criteria. The config file can be specified both
via an absolute path or via a relative path to the install dir

To activate the plugin in global mode, in :file:`plugin.config`, simply add::

   cache_fill.so --config <config-file>

To activate the plugin in per remap mode, in :file:`remap.config`, simply append the
below to the specific remap line::

   @plugin=cache_fill.so @pparam=<config-file>

include/exclude
---------------
The plugin supports a config file that can specify exclusion or inclusion of background fetch
based on any arbitrary header or client-ip

The contents of the config-file could be as below::

   include User-Agent ABCDEF
   exclude User-Agent *
   exclude Content-Type text
   exclude X-Foo-Bar text
   exclude Content-Length <1000
   exclude Client-IP 127.0.0.1
   include Client-IP 10.0.0.0/16

The ``include`` configuration directive is only used when there is a corresponding ``exclude`` to exempt.
For example, a single line directive, ``include Host example.com`` would not make the plugin
*only* act on example.com. To achieve classic allow (only) lists, one would need to have a broad
exclude line, such as::

   exclude Host *
   include Host example.com

range-request-only
------------------
When set to ``true``, this plugin will only trigger a background fetch if a range header is present.
Range headers include ``Range``, ``If-Match``, ``If-Modified-Since``, ``If-None-Match``, ``If-Range``
and ``If-Unmodified-Since``. By default, this is set to false.

This would look like::

    @plugin=cache_fill.so @pparam=--range-request-only

cache-range-req
---------------
When set to ``false``. this plugin will not trigger a background fetch for range requests. By default,
this is set to true.
Note: you cannot set this to false and ``range-request-only`` to true.

This would look like::

    @plugin=cache_fill.so @pparam=--cache-range-req=false

Functionality
-------------

Plugin decides to trigger a background fetch of the original (Client) request if the request/response is cacheable and cache status is TS_CACHE_LOOKUP_MISS/TS_CACHE_LOOKUP_HIT_STALE.
This will work for range requests by making a background fetch and removing the range header. To disable this feature, set ``--cache-range-req=false``

Future additions
----------------

*  Fetching the original request from the cache.

