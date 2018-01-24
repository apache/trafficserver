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

.. _admin-plugins-stale-while-revalidate:

  :deprecated:

Stale While Revalidate Plugin
=============================

The `Stale While Revalidate` plugin implements two extensions to the
`Cache-Control` header from `RFC 5861`_.

Both of these extensions give a window of time whereby stale assets
are allowed to be served with a Warning header line given that attempts
are being made to refresh the content.  With a correct implementation
a refresh can be successfully completed without a client request being
slowed for its delay.

From a client's perspective the plugin simply allows stale URLs to
be delivered as cache-stored fresh URLs for a correctly determined amount
of time.  Ideally a low-latency hit for any new URL content will quickly
appear if those windows of time have passed.

How it Works
------------

The plugin uses a global hook to detect client cache lookups that
result in `TS_CACHE_LOOKUP_HIT_STALE`.  From there the plugin uses both
configuration settings and the cached `Cache-Control` field to apply
the policy to the cache.  The main task at first is to determine if the lookup
can be changed to `TS_CACHE_LOOKUP_HIT_FRESH` under the rules of the
current cached doc.

1) stale-while-revalidate_



2) stale-if-error_

refresh content asynchronously while serving stale data

.. _RFC 5861: https://tools.ietf.org/html/rfc5861
.. _stale-while-revalidate: https://tools.ietf.org/html/rfc5861#section-3
.. _stale-if-error: https://tools.ietf.org/html/rfc5861#section-4
