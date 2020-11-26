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

.. _admin-plugins-background-fetch:
.. include:: /common.defs

Background Fetch Plugin
***********************


This is a plugin for |TS| that allows you to proactively fetch content from Origin in a way that it
will fill the object into cache. This is particularly useful when all (or most) of your client
requests are of the byte-Range type. The underlying problem being that Traffic Server is not able to
cache request / responses with byte ranges.

Using the plugin
----------------

This plugin functions as either a global or per remap plugin, and it takes an optional argument for
specifying a config file with inclusion or exclusion criteria. The config file can be specified both
via an absolute path or via a relative path to the install dir

To activate the plugin in global mode, in :file:`plugin.config`, simply add::

   background_fetch.so --config <config-file>

To activate the plugin in per remap mode, in :file:`remap.config`, simply append the
below to the specific remap line::

   @plugin=background_fetch.so @pparam=<config-file>

Functionality
-------------

Examining the responses from origin, we decide to trigger a background fetch of the original
(Client) request under these conditions:

*  The request is a ``GET`` request (we only support these right now)

*  The response is a ``206`` response

*  The original client request, and the Origin server response, is clearly indicating that the
   response is cacheable. This uses the new API :c:func:`TSHttpTxnIsCacheable()`, which also implies
   honoring current Traffic Server configurations.


Once deemed a good candidate to performance a background fetch, we'll replay the original client
request through the Traffic Server proxy again, except this time eliminating the ``Range`` header.
This is transparent to the original client request, which continues as normal.

Only one background fetch per URL is ever performed, making sure we do not accidentally put pressure
on the origin servers.

The plugin now supports a config file that can specify exclusion or inclusion of background fetch
based on any arbitrary header or client-ip::

   background_fetch.so --config <config-file>

The contents of the config-file could be as below::

   include User-Agent ABCDEF
   exclude User-Agent *
   exclude Content-Type text
   exclude X-Foo-Bar text
   exclude Content-Length <1000

.. important::

   The ``include`` configuration directive is only used when there is a corresponding ``exclude`` to exempt.
   For example, a single line directive, ``include Host example.com`` would not make the plugin
   *only* act on example.com. To acheive classic allow (only) lists, one would need to have a broad
   exclude line, such as::

      exclude Host *
      include Host example.com

The plugin also now supports per remap activation. To activate the plugin for a given remap, add the
below on the remap line::

   @plugin=background_fetch.so @pparam=<config-file>

Future additions
----------------

*  Limiting the background fetches to content of certain sizes.

