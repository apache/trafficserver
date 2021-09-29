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

The :program:`cache_promote` plugin provides a means to control when an object should
be allowed to enter the cache. This is orthogonal from normal ``Cache-Control``
directives, providing a different set of policies to apply. The typical use
case for this plugin is when you have a very large data set, where you want to
avoid churning the ATS cache for the long tail content.

All configuration is done via :file:`remap.config`, and the following options
are available:

.. program:: cache-promote

.. option:: --policy

   The promotion policy. The values ``lru`` and ``chance`` are supported.

.. option:: --sample

   The sampling rate for the request to be considered

If :option:`--policy` is set to ``lru`` the following options are also available:

.. option:: --label

   An optional label for this LRU, to allow sharing an LRU across multiple remap
   rules. Note: In order for an LRU to be used by multiple remap rules, not only
   must the label match, both the :option:`--hits` and  :option:`--buckets`
   options must be identical.

.. option:: --hits

   The minimum number of requests before promotion.

.. option:: --bytes

   In addition to requests, also count bytes that are cache misses. If specified,
   default is ``0``, whichever triggers first of bytes and requests (hits) will
   cause promotion.

.. option:: --buckets

   The size (number of entries) of the LRU.

.. option:: --stats-enable-with-id

   Enables collecting statistics.  The option requires an argument, the
   remap-identifier.  The remap-identifier is a string that is concatenated
   to the stat name.  The following stats are collected.

*  **plugin.cache_promote.${remap-identifier}.cache_hits** - Cache hit total, available for all policies.
*  **plugin.cache_promote.${remap-identifier}.freelist_size** - Size of the freelist when using the LRU policy.
*  **plugin.cache_promote.${remap-identifier}.lru_size** - Size of the LRU when using the LRU policy.
*  **plugin.cache_promote.${remap-identifier}.lru_hit** - LRU hit count when using the LRU policy.
*  **plugin.cache_promote.${remap-identifier}.lru_miss** - LRU miss count when using the LRU policy.
*  **plugin.cache_promote.${remap-identifier}.lru_vacated** - count of LRU entries removed to make room for a new request.
*  **plugin.cache_promote.${remap-identifier}.promoted** - count requests promoted, available in all policies.
*  **plugin.cache_promote.${remap-identifier}.total_requests** - count of all requests.

.. option:: --internal-enabled

   Allow cache promote to operate on internal (plugin-initiated) requests.

These options combined with your usage patterns will control how likely a
URL is to become promoted to enter the cache.

Examples
--------

These two examples shows how to use the chance and LRU policies, respectively::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=cache_promote.so @pparam=--policy=chance @pparam=--sample=10%

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=cache_promote.so @pparam=--policy=lru \
      @pparam=--hits=10 @pparam=--buckets=10000

Note :option:`--sample` is available for all policies and can be used to reduce pressure under heavy load.
