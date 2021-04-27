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

.. _admin-plugins-cache-range-requests:


Cache Range Requests Plugin
***************************

Description
===========

Most origin servers support HTTP/1.1 range requests (rfc 7233).
ATS internally handles range request caching in one of 2 ways:

* Don't cache range requests.
* Only server range requests from a wholly cached object.

This plugin allows you to remap individual range requests so that they
are stored as individual objects in the ATS cache when subsequent range
requests are likely to use the same range.  This spreads range requests
over multiple stripes thereby reducing I/O wait and system load averages.

:program:`cache_range_requests` reads the range request header byte range
value and then creates a new ``cache key URL`` using the original request
url with the range value appended to it.  The range header is removed
where appropriate from the requests and the origin server response code
is changed from a 206 to a 200 to insure that the object is written to
cache using the new cache key url.  The response code sent to the client
will be changed back to a 206 and all requests to the origin server will
contain the range header so that the correct response is received.

The :program:`cache_range_requests` plugin by itself has no logic to
efficiently manage overlapping ranges.  It is best to use this plugin
in conjunction with a smart client that only requests predetermined
non overlapping cache ranges (request blocking) or as a helper for the
:program:`slice` plugin.

Only requests which contain the ``Range: <units>=`` GET header
will be served by the :program:`cache_range_requests` plugin.

If/when ATS implements partial object caching this plugin will
become deprecated.

*NOTE* Given a multi range request the :program:`cache_range_requests`
only processes the first range and ignores the rest.

How to run the plugin
=====================

The plugin can run as a global plugin (a single global instance configured
using :file:`plugin.config`) or as per-remap plugin (a separate instance
configured per remap rule in :file:`remap.config`).

Global instance
---------------

.. code::

  $ cat plugin.config
  cache_range_request.so


Per-remap instance
------------------

.. code::

  $cat remap.config
  map http://www.example.com http://www.origin.com \
      @plugin=cache_range_requests.so


If both global and per-remap instance are used the per-remap configuration
would take precedence (per-remap configuration would be applied and the
global configuration ignored).

Plugin options
==============


Parent Selection as Cache Key
-----------------------------

.. option:: --ps-cachekey
.. option:: -p

Without this option parent selection is based solely on the hash of a
URL Path a URL is requested from the same upstream parent cache listed
in parent.config


With this option parent selection is based on the full ``cache key URL``
which includes information about the partial content range.  In this mode,
all requests (include partial content) will use consistent hashing method
for parent selection.


X-Crr-Ims header support
------------------------

.. option:: --consider-ims
.. option:: -c

To support slice plugin self healing an option to force revalidation
after cache lookup complete was added.  This option is triggered by a
special header:

.. code::

    X-Crr-Ims: Tue, 19 Nov 2019 13:26:45 GMT

When this header is provided and a `cache hit fresh` is encountered the
``Date`` header of the object in cache is compared to this header date
value.  If the cache date is *less* than this IMS date then the object
is marked as STALE and an appropriate If-Modified-Since or If-Match
request along with this X-Crr-Ims header is passed up to the parent.

In order for this to properly work in a CDN each cache in the
chain *SHOULD* also contain a remap rule with the
:program:`cache_range_requests` plugin with this option set.

Don't modify the Cache Key
--------------------------

.. option:: --no-modify-cachekey
.. option:: -n

With each transaction TSCacheUrlSet may only be called once.  When
using the `cache_range_requests` plugin in conjunction with the
`cachekey` plugin the option `--include-headers=Range` should be
added as a `cachekey` parameter with this option.  Configuring this
incorrectly *WILL* result in cache poisoning.

.. code::

       map http://ats/ http://parent/ \
           @plugin=cachekey.so @pparam=--include-headers=Range \
           @plugin=cache_range_requests.so @pparam=--no-modify-cachekey

*Without this `cache_range_requests` plugin option*

*IF* the TSCacheUrlSet call in cache_range_requests fails, an error is
generated in the logs and the cache_range_requests plugin will disable
transaction caching in order to avoid cache poisoning.

Configuration examples
======================

Global plugin
-------------

.. code::

    cache_range_requests.so --ps-cachekey --consider-ims --no-modify-cachekey

or

.. code::

    cache_range_requests.so -p -c -n

Remap plugin
------------

.. code::

    map http://ats http://parent @plugin=cache_range_requests.so @pparam=--ps-cachekey @pparam=--consider-ims @pparam=--no-modify-cachekey

or

.. code::

    map http://ats http://parent @plugin=cache_range_requests.so @pparam=-p @pparam=-c @pparam=-n
