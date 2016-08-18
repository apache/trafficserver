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

.. _admin-plugins-cache-promote:

Cache Promote Plugin
********************

The `cache_promote` plugin provides a means to control when an object should
be allowed to enter the cache. This is orthogonal from normal Cache-Control
directives, providing a different set of policies to apply. The typical use
case for this plugin is when you have a very large data set, where you want to
avoid churning the ATS cache for the long tail content.

All configuration is done via :file:`remap.config`, and the following options
are available:

    --policy   The promotion policy. `lru` and `chance` are supported
    --sample   The sampling rate for the request to be considered

For the `lru` plugin, the following options are also available:

    --hits     The minimum number of hits before promotion
    --buckets  The size (entries) of the LRU

These two options combined with your usage patterns will control how likely a
URL is to become promoted to enter the cache.

Examples
--------

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=cache_promote.so @pparam=--policy=chance --sample=10%

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=cache_promote.so @pparam=--policy=lru \
      @pparam=--hits=10 @pparam=--buckets=10000

Note that the `--sample` option is available for all policies, and when used
to reduce pressure on the under heavy load.
