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


Using the plugin
----------------

This plugin functions as a per remap plugin.

To activate the plugin, in :file:`remap.config`, simply append the
below to the specific remap line::

   @plugin=cache_fill.so @pparam=<config-file>

Functionality
-------------

Plugin decides to trigger a background fetch of the original (Client) request if the request/response is cacheable and cache status is TS_CACHE_LOOKUP_MISS/TS_CACHE_LOOKUP_HIT_STALE.

Future additions
----------------

*  Fetching the original request from the cache.

