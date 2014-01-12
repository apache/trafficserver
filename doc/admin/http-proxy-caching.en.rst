.. _http-proxy-caching:

HTTP Proxy Caching
******************

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

Web proxy caching enables you to store copies of frequently-accessed web
objects (such as documents, images, and articles) and then serve this
information to users on demand. It improves performance and frees up
Internet bandwidth for other tasks.

.. toctree::
   :maxdepth: 2

Understanding HTTP Web Proxy Caching
====================================

Internet users direct their requests to web servers all over the
Internet. A caching server must act as a **web proxy server** so it can
serve those requests. After a web proxy server receives requests for web
objects, it either serves the requests or forwards them to the **origin
server** (the web server that contains the original copy of the
requested information). The Traffic Server proxy supports **explicit
proxy caching**, in which the user's client software must be configured
to send requests directly to the Traffic Server proxy. The following
overview illustrates how Traffic Server serves a request.

1. Traffic Server receives a client request for a web object.

2. Using the object address, Traffic Server tries to locate the
   requested object in its object database (**cache**).

3. If the object is in the cache, then Traffic Server checks to see if
   the object is fresh enough to serve. If it is fresh, then Traffic
   Server serves it to the client as a **cache hit** (see the figure
   below).

   .. figure:: ../static/images/admin/cache_hit.jpg
      :align: center
      :alt: A cache hit

      A cache hit

4. If the data in the cache is stale, then Traffic Server connects to
   the origin server and checks if the object is still fresh (a
   :term:`revalidation`). If it is, then Traffic Server immediately sends
   the cached copy to the client.

5. If the object is not in the cache (a **cache miss**) or if the server
   indicates the cached copy is no longer valid, then Traffic Server
   obtains the object from the origin server. The object is then
   simultaneously streamed to the client and the Traffic Server local
   cache (see the figure below). Subsequent requests for the object can
   be served faster because the object is retrieved directly from cache.

   .. figure:: ../static/images/admin/cache_miss.jpg
      :align: center
      :alt: A cache miss

      A cache miss

Caching is typically more complex than the preceding overview suggests.
In particular, the overview does not discuss how Traffic Server ensures
freshness, serves correct HTTP alternates, and treats requests for
objects that cannot/should not be cached. The following sections discuss
these issues in greater detail.

Ensuring Cached Object Freshness
================================

When Traffic Server receives a request for a web object, it first tries
to locate the requested object in its cache. If the object is in cache,
then Traffic Server checks to see if the object is fresh enough to
serve. For HTTP objects, Traffic Server supports optional
author-specified expiration dates. Traffic Server adheres to these
expiration dates; otherwise, it picks an expiration date based on how
frequently the object is changing and on administrator-chosen freshness
guidelines. Objects can also be revalidated by checking with the origin
server to see if an object is still fresh.

HTTP Object Freshness
---------------------

Traffic Server determines whether an HTTP object in the cache is fresh
by:

-  **Checking the** ``Expires`` **or** ``max-age`` **header**

   Some HTTP objects contain ``Expires`` headers or ``max-age`` headers
   that explicitly define how long the object can be cached. Traffic
   Server compares the current time with the expiration time to
   determine if the object is still fresh.

-  **Checking the** ``Last-Modified`` **/** ``Date`` **header**

   If an HTTP object has no ``Expires`` header or ``max-age`` header,
   then Traffic Server can calculate a freshness limit using the
   following formula::

      freshness_limit = ( date - last_modified ) * 0.10

   where *date* is the date in the object's server response header
   and *last_modified* is the date in the ``Last-Modified`` header.
   If there is no ``Last-Modified`` header, then Traffic Server uses the
   date the object was written to cache. The value ``0.10`` (10 percent)
   can be increased or reduced to better suit your needs (refer to
   `Modifying Aging Factor for Freshness Computations`_).

   The computed freshness limit is bound by a minimum and maximum value
   - refer to `Setting Absolute Freshness Limits`_ for more information.

-  **Checking the absolute freshness limit**

   For HTTP objects that do not have ``Expires`` headers or do not have
   both ``Last-Modified`` and ``Date`` headers, Traffic Server uses a
   maximum and minimum freshness limit (refer to `Setting Absolute Freshness Limits`_).

-  **Checking revalidate rules in the** :file:`cache.config` **file**

   Revalidate rules apply freshness limits to specific HTTP objects. You
   can set freshness limits for objects originating from particular
   domains or IP addresses, objects with URLs that contain specified
   regular expressions, objects requested by particular clients, and so
   on (refer to :file:`cache.config`).

Modifying Aging Factor for Freshness Computations
-------------------------------------------------

If an object does not contain any expiration information, then Traffic
Server can estimate its freshness from the ``Last-Modified`` and
``Date`` headers. By default, Traffic Server stores an object for 10% of
the time that elapsed since it last changed. You can increase or reduce
the percentage according to your needs.

To modify the aging factor for freshness computations

1. Change the value for :ts:cv:`proxy.config.http.cache.heuristic_lm_factor`.

2. Run the :option:`traffic_line -x` command to apply the configuration
   changes.

Setting absolute Freshness Limits
---------------------------------

Some objects do not have ``Expires`` headers or do not have both
``Last-Modified`` and ``Date`` headers. To control how long these
objects are considered fresh in the cache, specify an **absolute
freshness limit**.

To specify an absolute freshness limit

1. Edit the variables

   -  :ts:cv:`proxy.config.http.cache.heuristic_min_lifetime`
   -  :ts:cv:`proxy.config.http.cache.heuristic_max_lifetime`

2. Run the :option:`traffic_line -x` command to apply the configuration
   changes.

Specifying Header Requirements
------------------------------

To further ensure freshness of the objects in the cache, configure
Traffic Server to cache only objects with specific headers. By default,
Traffic Server caches all objects (including objects with no headers);
you should change the default setting only for specialized proxy
situations. If you configure Traffic Server to cache only HTTP objects
with ``Expires`` or ``max-age`` headers, then the cache hit rate will be
noticeably reduced (since very few objects will have explicit expiration
information).

To configure Traffic Server to cache objects with specific headers

1. Change the value for :ts:cv:`proxy.config.http.cache.required_headers`.

2. Run the :option:`traffic_line -x` command to apply the configuration
   changes.

.. _cache-control-headers:

Cache-Control Headers
---------------------

Even though an object might be fresh in the cache, clients or servers
often impose their own constraints that preclude retrieval of the object
from the cache. For example, a client might request that a object *not*
be retrieved from a cache, or if it does, then it cannot have been
cached for more than 10 minutes. Traffic Server bases the servability of
a cached object on ``Cache-Control`` headers that appear in both client
requests and server responses. The following ``Cache-Control`` headers
affect whether objects are served from cache:

-  The ``no-cache`` header, sent by clients, tells Traffic Server that
   it should not serve any objects directly from the cache;
   therefore, Traffic Server will always obtain the object from the
   origin server. You can configure Traffic Server to ignore client
   ``no-cache`` headers - refer to `Configuring Traffic Server to Ignore Client no-cache Headers`_
   for more information.

-  The ``max-age`` header, sent by servers, is compared to the object
   age. If the age is less than ``max-age``, then the object is fresh
   and can be served.

-  The ``min-fresh`` header, sent by clients, is an **acceptable
   freshness tolerance**. This means that the client wants the object to
   be at least this fresh. Unless a cached object remains fresh at least
   this long in the future, it is revalidated.

-  The ``max-stale`` header, sent by clients, permits Traffic Server to
   serve stale objects provided they are not too old. Some browsers
   might be willing to take slightly stale objects in exchange for
   improved performance, especially during periods of poor Internet
   availability.

Traffic Server applies ``Cache-Control`` servability criteria
***after*** HTTP freshness criteria. For example, an object might be
considered fresh but will not be served if its age is greater than its
``max-age``.

Revalidating HTTP Objects
-------------------------

When a client requests an HTTP object that is stale in the cache,
Traffic Server revalidates the object. A **revalidation** is a query to
the origin server to check if the object is unchanged. The result of a
revalidation is one of the following:

-  If the object is still fresh, then Traffic Server resets its
   freshness limit and serves the object.

-  If a new copy of the object is available, then Traffic Server caches
   the new object (thereby replacing the stale copy) and simultaneously
   serves the object to the client.

-  If the object no longer exists on the origin server, then Traffic
   Server does not serve the cached copy.

-  If the origin server does not respond to the revalidation query, then
   Traffic Server serves the stale object along with a
   ``111 Revalidation Failed`` warning.

By default, Traffic Server revalidates a requested HTTP object in the
cache if it considers the object to be stale. Traffic Server evaluates
object freshness as described in `HTTP Object Freshness`_.
You can reconfigure how Traffic Server evaluates freshness by selecting
one of the following options:

-  Traffic Server considers all HTTP objects in the cache to be stale:
   always revalidate HTTP objects in the cache with the origin server.
-  Traffic Server considers all HTTP objects in the cache to be fresh:
   never revalidate HTTP objects in the cache with the origin server.
-  Traffic Server considers all HTTP objects without ``Expires`` or
   ``Cache-control`` headers to be stale: revalidate all HTTP objects
   without ``Expires`` or ``Cache-Control`` headers.

To configure how Traffic Server revalidates objects in the cache, you
can set specific revalidation rules in :file:`cache.config`.

To configure revalidation options

1. Edit the following variable in :file:`records.config`

   -  :ts:cv:`proxy.config.http.cache.when_to_revalidate`

2. Run the :option:`traffic_line -x` command to apply the configuration
   changes.

Scheduling Updates to Local Cache Content
=========================================

To further increase performance and to ensure that HTTP objects are
fresh in the cache, you can use the **Scheduled Update** option. This
configures Traffic Server to load specific objects into the cache at
scheduled times. You might find this especially beneficial in a reverse
proxy setup, where you can *preload* content you anticipate will be in
demand.

To use the Scheduled Update option, you must perform the following
tasks.

-  Specify the list of URLs that contain the objects you want to
   schedule for update,
-  the time the update should take place,
-  and the recursion depth for the URL.
-  Enable the scheduled update option and configure optional retry
   settings.

Traffic Server uses the information you specify to determine URLs for
which it is responsible. For each URL, Traffic Server derives all
recursive URLs (if applicable) and then generates a unique URL list.
Using this list, Traffic Server initiates an HTTP ``GET`` for each
unaccessed URL. It ensures that it remains within the user-defined
limits for HTTP concurrency at any given time. The system logs the
completion of all HTTP ``GET`` operations so you can monitor the
performance of this feature.

Traffic Server also provides a **Force Immediate Update** option that
enables you to update URLs immediately without waiting for the specified
update time to occur. You can use this option to test your scheduled
update configuration (refer to `Forcing an Immediate Update`_).

Configuring the Scheduled Update Option
---------------------------------------

To configure the scheduled update option

1. Edit :file:`update.config` to
   enter a line in the file for each URL you want to update.
2. Edit the following variables

   -  :ts:cv:`proxy.config.update.enabled`
   -  :ts:cv:`proxy.config.update.retry_count`
   -  :ts:cv:`proxy.config.update.retry_interval`
   -  :ts:cv:`proxy.config.update.concurrent_updates`

3. Run the :option:`traffic_line -x` command to apply the configuration
   changes.

Forcing an Immediate Update
---------------------------

Traffic Server provides a **Force Immediate Update** option that enables
you to immediately verify the URLs listed in :file:`update.config`.
The Force Immediate Update option disregards the offset hour and
interval set in :file:`update.config` and immediately updates the
URLs listed.

To configure the Force Immediate Update option

1. Edit the following variables

   -  :ts:cv:`proxy.config.update.force`
   -  Make sure :ts:cv:`proxy.config.update.enabled` is set to 1.

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. important::

   When you enable the Force Immediate Update option, Traffic Server continually updates the URLs specified in
   :file:`update.config` until you disable the option. To disable the Force Immediate Update option, set
   :ts:cv:`proxy.config.update.force` to ``0`` (zero).

Pushing Content into the Cache
==============================

Traffic Server supports the HTTP ``PUSH`` method of content delivery.
Using HTTP ``PUSH``, you can deliver content directly into the cache
without client requests.

Configuring Traffic Server for PUSH Requests
--------------------------------------------

Before you can deliver content into your cache using HTTP ``PUSH``, you
must configure Traffic Server to accept ``PUSH`` requests.

To configure Traffic Server to accept ``PUSH`` requests

1. Edit :file:`ip_allow.config` to allow ``PUSH``.

2. Edit the following variable in :file:`records.config`, enable
   the push_method.

   -  :ts:cv:`proxy.config.http.push_method_enabled`

3. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Understanding HTTP PUSH
-----------------------

``PUSH`` uses the HTTP 1.1 message format. The body of a ``PUSH``
request contains the response header and response body that you want to
place in the cache. The following is an example of a ``PUSH`` request::

   PUSH http://www.company.com HTTP/1.0
   Content-length: 84

   HTTP/1.0 200 OK
   Content-type: text/html
   Content-length: 17

   <HTML>
   a
   </HTML>

.. important::

   Your header must include ``Content-length`` - ``Content-length`` must include both ``header`` and ``body byte
   count``.

Tools that will help manage pushing
-----------------------------------

There is a perl script for pushing, :program:`tspush`,
which can help you understanding how to write scripts for pushing
content yourself.

Pinning Content in the Cache
============================

The **Cache Pinning Option** configures Traffic Server to keep certain
HTTP objects in the cache for a specified time. You can use this option
to ensure that the most popular objects are in cache when needed and to
prevent Traffic Server from deleting important objects. Traffic Server
observes ``Cache-Control`` headers and pins an object in the cache only
if it is indeed cacheable.

To set cache pinning rules

1. Make sure the following variable in :file:`records.config` is set

   -  :ts:cv:`proxy.config.cache.permit.pinning`

2. Add a rule in :file:`cache.config` for each
   URL you want Traffic Server to pin in the cache. For example::

      url_regex=^https?://(www.)?apache.org/dev/ pin-in-cache=12h

3. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

To Cache or Not to Cache?
=========================

When Traffic Server receives a request for a web object that is not in
the cache, it retrieves the object from the origin server and serves it
to the client. At the same time, Traffic Server checks if the object is
cacheable before storing it in its cache to serve future requests.

Caching HTTP Objects
====================

Traffic Server responds to caching directives from clients and origin
servers, as well as directives you specify through configuration options
and files.

Client Directives
-----------------

By default, Traffic Server does *not* cache objects with the following
**request headers**:

-  ``Authorization``: header

-  ``Cache-Control: no-store`` header

-  ``Cache-Control: no-cache`` header

   To configure Traffic Server to ignore the ``Cache-Control: no-cache``
   header, refer to `Configuring Traffic Server to Ignore Client no-cache Headers`_

-  ``Cookie``: header (for text objects)

   By default, Traffic Server caches objects served in response to
   requests that contain cookies (unless the object is text). You can
   configure Traffic Server to not cache cookied content of any type,
   cache all cookied content, or cache cookied content that is of image
   type only. For more information, refer to `Caching Cookied Objects`_.

Configuring Traffic Server to Ignore Client no-cache Headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, Traffic Server strictly observes client
``Cache-Control: no-cache`` directives. If a requested object contains a
``no-cache`` header, then Traffic Server forwards the request to the
origin server even if it has a fresh copy in cache. You can configure
Traffic Server to ignore client ``no-cache`` directives such that it
ignores ``no-cache`` headers from client requests and serves the object
from its cache.

To configure Traffic Server to ignore client ``no-cache`` headers

1. Edit the following variable in :file:`records.config`

   -  :ts:cv:`proxy.config.http.cache.ignore_client_no_cache`

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Origin Server Directives
------------------------

By default, Traffic Server does *not* cache objects with the following
**response headers**:

-  ``Cache-Control: no-store`` header
-  ``Cache-Control: private`` header
-  ``WWW-Authenticate``: header

   To configure Traffic Server to ignore ``WWW-Authenticate`` headers,
   refer to `Configuring Traffic Server to Ignore WWW-Authenticate Headers`_.

-  ``Set-Cookie``: header
-  ``Cache-Control: no-cache`` headers

   To configure Traffic Server to ignore ``no-cache`` headers, refer to
   `Configuring Traffic Server to Ignore Server no-cache Headers`_.

-  ``Expires``: header with value of 0 (zero) or a past date

Configuring Traffic Server to Ignore Server no-cache Headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, Traffic Server strictly observes ``Cache-Control: no-cache``
directives. A response from an origin server with a ``no-cache`` header
is not stored in the cache and any previous copy of the object in the
cache is removed. If you configure Traffic Server to ignore ``no-cache``
headers, then Traffic Server also ignores ``no-store`` headers. The
default behavior of observing ``no-cache`` directives is appropriate
in most cases.

To configure Traffic Server to ignore server ``no-cache`` headers

#. Edit the variable :ts:cv:`proxy.config.http.cache.ignore_server_no_cache`

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Configuring Traffic Server to Ignore WWW-Authenticate Headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, Traffic Server does not cache objects that contain
``WWW-Authenticate`` response headers. The ``WWW-Authenticate`` header
contains authentication parameters the client uses when preparing the
authentication challenge response to an origin server.

When you configure Traffic Server to ignore origin server
``WWW-Authenticate`` headers, all objects with ``WWW-Authenticate``
headers are stored in the cache for future requests. However, the
default behavior of not caching objects with ``WWW-Authenticate``
headers is appropriate in most cases. Only configure Traffic Server to
ignore server ``WWW-Authenticate`` headers if you are knowledgeable
about HTTP 1.1.

To configure Traffic Server to ignore server ``WWW-Authenticate``
headers

#. Edit the variable :ts:cv:`proxy.config.http.cache.ignore_authentication`

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Configuration Directives
------------------------

In addition to client and origin server directives, Traffic Server
responds to directives you specify through configuration options and
files.

You can configure Traffic Server to do the following:

-  *Not* cache any HTTP objects (refer to `Disabling HTTP Object Caching`_).
-  Cache **dynamic content** - that is, objects with URLs that end in
   ``.asp`` or contain a question mark (``?``), semicolon
   (**``;``**), or **``cgi``**. For more information, refer to `Caching Dynamic Content`_.
-  Cache objects served in response to the ``Cookie:`` header (refer to
   `Caching Cookied Objects`_.
-  Observe ``never-cache`` rules in the :file:`cache.config` file.

Disabling HTTP Object Caching
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, Traffic Server caches all HTTP objects except those for
which you have set ``never-cache`` as :ref:`action rules <cache-config-format-action>`
in the :file:`cache.config` file. You can disable HTTP object
caching so that all HTTP objects are served directly from the origin
server and never cached, as detailed below.

To disable HTTP object caching manually

1. Set the variable :ts:cv:`proxy.config.http.enabled` to ``0``.

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Caching Dynamic Content
~~~~~~~~~~~~~~~~~~~~~~~

A URL is considered **dynamic** if it ends in **``.asp``** or contains a
question mark (``?``), a semicolon (``;``), or ``cgi``. By
default, Traffic Server caches dynamic content. You can configure the
system to ignore dyanamic looking content, although this is recommended
only if the content is *truely* dyanamic, but fails to advertise so with
appropriate ``Cache-Control`` headers.

To configure Traffic Server's cache behaviour in regard to dynamic
content

1. Edit the following variable in :file:`records.config`

   -  :ts:cv:`proxy.config.http.cache.cache_urls_that_look_dynamic`

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Caching Cookied Objects
~~~~~~~~~~~~~~~~~~~~~~~

.. XXX This should be extended to xml as well!

By default, Traffic Server caches objects served in response to requests
that contain cookies. This is true for all types of objects except for
text. Traffic Server does not cache cookied text content because object
headers are stored along with the object, and personalized cookie header
values could be saved with the object. With non-text objects, it is
unlikely that personalized headers are delivered or used.

You can reconfigure Traffic Server to:

-  *Not* cache cookied content of any type.
-  Cache cookied content that is of image type only.
-  Cache all cookied content regardless of type.

To configure how Traffic Server caches cookied content

1. Edit the variable :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Forcing Object Caching
======================

You can force Traffic Server to cache specific URLs (including dynamic
URLs) for a specified duration, regardless of ``Cache-Control`` response
headers.

To force document caching

1. Add a rule for each URL you want Traffic Server to pin to the cache
   :file:`cache.config`::

       url_regex=^https?://(www.)?apache.org/dev/ ttl-in-cache=6h

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Caching HTTP Alternates
=======================

Some origin servers answer requests to the same URL with a variety of
objects. The content of these objects can vary widely, according to
whether a server delivers content for different languages, targets
different browsers with different presentation styles, or provides
different document formats (HTML, XML). Different versions of the same
object are termed **alternates** and are cached by Traffic Server based
on ``Vary`` response headers. You can specify additional request and
response headers for specific ``Content-Type``\s that Traffic Server
will identify as alternates for caching. You can also limit the number
of alternate versions of an object allowed in the cache.

Configuring How Traffic Server Caches Alternates
------------------------------------------------

To configure how Traffic Server caches alternates, follow the steps
below

1. Edit the following variables

   -  :ts:cv:`proxy.config.http.cache.enable_default_vary_headers`
   -  :ts:cv:`proxy.config.http.cache.vary_default_text`
   -  :ts:cv:`proxy.config.http.cache.vary_default_images`
   -  :ts:cv:`proxy.config.http.cache.vary_default_other`

2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. note::

   If you specify ``Cookie`` as the header field on which to vary
   in the above variables, make sure that the variable
   :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`
   is set appropriately.

Limiting the Number of Alternates for an Object
-----------------------------------------------

You can limit the number of alternates Traffic Server can cache per
object (the default is 3).

.. important::

   Large numbers of alternates can affect Traffic Server
   cache performance because all alternates have the same URL. Although
   Traffic Server can look up the URL in the index very quickly, it must
   scan sequentially through available alternates in the object store.

   To limit the number of alternates

   #. Edit the variable :ts:cv:`proxy.config.cache.limits.http.max_alts`
   #. Run the command :option:`traffic_line -x` to apply the configuration changes.


.. _using-congestion-control:

Using Congestion Control
========================

The **Congestion Control** option enables you to configure Traffic
Server to stop forwarding HTTP requests to origin servers when they
become congested. Traffic Server then sends the client a message to
retry the congested origin server later.

To use the **Congestion Control** option, you must perform the following
tasks:

#. Set the variable :ts:cv:`proxy.config.http.congestion_control.enabled` to ``1``

   -  Create rules in the :file:`congestion.config` file to specify:
   -  which origin servers Traffic Server tracks for congestion
   -  the timeouts Traffic Server uses, depending on whether a server is
      congested
   -  the page Traffic Server sends to the client when a server becomes
      congested
   -  if Traffic Server tracks the origin servers per IP address or per
      hostname

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. _transaction-buffering-control:

Using Transaction Buffering Control
===================================

By default I/O operations are run at full speed, as fast as either Traffic Server, the network, or the cache can go.
This can be problematic for large objects if the client side connection is significantly slower. In such cases the
content will be buffered in ram while waiting to be sent to the client. This could potentially also happen for ``POST``
requests if the client connection is fast and the origin server connection slow. If very large objects are being used
this can cause the memory usage of Traffic Server to become `very large
<https://issues.apache.org/jira/browse/TS-1496>`_.

This problem can be ameloriated by controlling the amount of buffer space used by a transaction. A high water and low
water mark are set in terms of bytes used by the transaction. If the buffer space in use exceeds the high water mark,
the connection is throttled to prevent additional external data from arriving. Internal operations continue to proceed
at full speed until the buffer space in use drops below the low water mark and external data I/O is re-enabled.

Although this is intended primarily to limit the memory usage of Traffic Server it can also serve as a crude rate
limiter by setting a buffer limit and then throttling the client side connection either externally or via a transform.
This will cause the connection to the origin server to be limited to roughly the client side connection speed.

Traffic Server does network I/O in large chunks (32K or so) and therefore the granularity of transaction buffering
control is limited to a similar precision.

The buffer size calculations include all elements in the transaction, including any buffers associated with :ref:`transform plugins <transform-plugin>`.

Transaction buffering control can be enabled globally by using configuration variables or by :c:func:`TSHttpTxnConfigIntSet` in a plugin.

================= ================================================== ========================================
Value             Variable                                           `TSHttpTxnConfigIntSet` key
================= ================================================== ========================================
Enable buffering  :ts:cv:`proxy.config.http.flow_control.enabled`    `TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED`
Set high water    :ts:cv:`proxy.config.http.flow_control.high_water` `TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER`
Set low water     :ts:cv:`proxy.config.http.flow_control.low_water`  `TS_CONFIG_HTTP_FLOW_CONTROL_LOW_WATER`
================= ================================================== ========================================

Be careful to always have the low water mark equal or less than the high water mark. If you set only one, the other will
be set to the same value.

If using c:func:`TSHttpTxnConfigIntSet`, it must be called no later than `TS_HTTP_READ_RESPONSE_HDR_HOOK`.

.. _reducing-origin-server-requests-avoiding-the-thundering-herd:

Reducing Origin Server Requests (Avoiding the Thundering Herd)
==============================================================

When an object can not be served from cache, the request will be proxied to the origin server. For a popular object,
this can result in many near simultaneous requests to the origin server, potentially overwhelming it or associated
resources. There are several features in Traffic Server that can be used to avoid this scenario.

Read While Writer
-----------------
When Traffic Server goes to fetch something from origin, and upon receiving the response, any number of clients can be allowed to start serving the partially filled cache object once background_fill_completed_threshold % of the object has been received. The difference is that Squid allows this as soon as it goes to origin, whereas ATS can not do it until we get the complete response header. The reason for this is that we make no distinction between cache refresh, and cold cache, so we have no way to know if a response is going to be cacheable, and therefore allow read-while-writer functionality.

The configurations necessary to enable this in ATS are:

|   CONFIG :ts:cv:`proxy.config.cache.enable_read_while_writer` ``INT 1``
|   CONFIG :ts:cv:`proxy.config.http.background_fill_active_timeout` ``INT 0``
|   CONFIG :ts:cv:`proxy.config.http.background_fill_completed_threshold` ``FLOAT 0.000000``
|   CONFIG :ts:cv:`proxy.config.cache.max_doc_size` ``INT 0`` 

All four configurations are required, for the following reasons:

-  enable_read_while_writer turns the feature on. It's off (0) by default
-  The background fill feature should be allowed to kick in for every possible request. This is necessary, in case the writer ("first client session") goes away, someone needs to take over the session. Hence, you should set the background fill timeouts and threshold to zero; this assures they never times out and always is allowed to kick in. 
-  The proxy.config.cache.max_doc_size should be unlimited (set to 0), since the object size may be unknown, and going over this limit would cause a disconnect on the objects being served.

Once all this enabled, you have something that is very close, but not quite the same, as Squid's Collapsed Forwarding.



.. _fuzzy-revalidation:

Fuzzy Revalidation
------------------
Traffic Server can be set to attempt to revalidate an object before it becomes stale in cache. :file:`records.config` contains the settings:

|   CONFIG :ts:cv:`proxy.config.http.cache.fuzz.time` ``INT 240``
|   CONFIG :ts:cv:`proxy.config.http.cache.fuzz.min_time` ``INT 0``
|   CONFIG :ts:cv:`proxy.config.http.cache.fuzz.probability` ``FLOAT 0.005``

For every request for an object that occurs "fuzz.time" before (in the example above, 240 seconds) the object is set to become stale, there is a small
chance (fuzz.probability == 0.5%) that the request will trigger a revalidation request to the origin. For objects getting a few requests per second, this would likely not trigger, but then this feature is not necessary anyways since odds are only 1 or a small number of connections would hit origin upon objects going stale. The defaults are a good compromise, for objects getting roughly 4 requests / second or more, it's virtually guaranteed to trigger a revalidate event within the 240s. These configs are also overridable per remap rule or via a plugin, so can be adjusted per request if necessary.  

Note that if the revalidation occurs, the requested object is no longer available to be served from cache.  Subsequent
requests for that object will be proxied to the origin. 

Finally, the fuzz.min_time is there to be able to handle requests with a TTL less than fuzz.time – it allows for different times to evaluate the probability of revalidation for small TTLs and big TTLs. Objects with small TTLs will start "rolling the revalidation dice" near the fuzz.min_time, while objects with large TTLs would start at fuzz.time. A logarithmic like function between determines the revalidation evaluation start time (which will be between fuzz.min_time and fuzz.time). As the object gets closer to expiring, the window start becomes more likely. By default this setting is not enabled, but should be enabled anytime you have objects with small TTLs. Note that this option predates overridable configurations, so you can achieve something similar with a plugin or remap.config conf_remap.so configs.

These configurations are similar to Squid's refresh_stale_hit configuration option.


Open Read Retry Timeout
-----------------------

The open read retry configurations attempt to reduce the number of concurrent requests to the origin for a given object. While an object is being fetched from the origin server, subsequent requests would wait open_read_retry_time milliseconds before checking if the object can be served from cache. If the object is still being fetched, the subsequent requests will retry max_open_read_retries times. Thus, subsequent requests may wait a total of (max_open_read_retries x open_read_retry_time) milliseconds before establishing an origin connection of its own. For instance, if they are set to 5 and 10 respectively, connections will wait up to 50ms for a response to come back from origin from a previous request, until this request is allowed through.

These settings are inappropriate when objects are uncacheable. In those cases, requests for an object effectively become serialized. The subsequent requests would await at least open_read_retry_time milliseconds before being proxies to the origin.

Similarly, this setting should be used in conjunction with Read While Writer for big (those that take longer than (max_open_read_retries x open_read_retry_time) milliseconds to transfer) cacheable objects. Without the read-while-writer settings enabled, while the initial fetch is ongoing, not only would subsequent requests be delayed by the maximum time, but also, those requests would result in another request to the origin server.

Since ATS now supports setting these settings per-request or remap rule, you can configure this to be suitable for your setup much more easily.

The configurations are (with defaults):

|   CONFIG :ts:cv:`proxy.config.http.cache.max_open_read_retries` ``INT -1``
|   CONFIG :ts:cv:`proxy.config.http.cache.open_read_retry_time` ``INT 10``

The default means that the feature is disabled, and every connection is allowed to go to origin instantly. When enabled, you will try max_open_read_retries times, each with a open_read_retry_time timeout.
