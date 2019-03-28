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

.. configfile:: cache.config

cache.config
************

The :file:`cache.config` file allows you to overrule the origin's cache
policies. You can add caching rules to specify the following:

- Not to cache objects from specific IP addresses.
- How long to pin particular objects in the cache.
- How long to consider cached objects as fresh.
- Whether to ignore no-cache directives from the server.

.. important::

   Generally, using this file to define cache policies is an antipattern.
   It's usually better to have the origin specify the cache policy via the
   `Cache-Control: <https://tools.ietf.org/html/rfc7234#section-5.2>`_ header.
   That way, all the business logic stays with the content generation. The
   origin is in a much better position to know which content can be safely
   cached, and for how long. It can make fine grained decisions, changing
   Cache-Control: header value per object. This file allows for some overrides
   but, is relatively crude compared to what the origin can provide.

   After modifying :file:`cache.config`, run :option:`traffic_ctl config reload`
   to apply changes.

Format
======

Each line in the :file:`cache.config` file contains a caching rule. |TS|
recognizes three space-delimited tags::

   primary_destination=value secondary_specifier=value action=value

You can use more than one secondary specifier in a rule. However, you
cannot repeat a secondary specifier. The following list shows the
possible primary destinations with allowed values.

Primary Destinations
--------------------

The primary destination field on each line is used to restrict the requests to
which the caching rule will apply.

.. _cache-config-format-dest-domain:

``dest_domain``
   A requested domain name. |TS| matches the host name of the destination from
   the URL in the request.

.. _cache-config-format-dest-host:

``dest_host``
   Alias for ``dest_domain``.

.. _cache-config-format-dest-ip:

``dest_ip``
   A requested IP address. |TS| matches the IP address of the destination in
   the request.

.. _cache-config-format-dest-host-regex:

``host_regex``
   A regular expression to be tested against the destination host name in the
   request.

.. _cache-config-format-url-regex:

``url_regex``
   A regular expression to be tested against the URL in the request.

Secondary Specifiers
--------------------

The secondary specifiers are optional and may be used to further restrict
which requests are impacted by the caching rule. Multiple secondary specifiers
may be used within a single rule, though each type of specifier can appear at
most one time. In other words, you may have both a ``port`` and ``scheme`` in
the same rule, but you may not have two ``port``\ s.

.. _cache-config-format-port:

``port``
   Request URL port.

.. _cache-config-format-scheme:

``scheme``
   Request URL protocol (http or https).

.. _cache-config-format-prefix:

``prefix``
   Prefix in the path part of a URL.

.. _cache-config-format-suffix:

``suffix``
   File suffix in the URL.

.. _cache-config-format-method:

``method``
   Request URL method (get, put, post, trace, etc.).

.. _cache-config-format-time:

``time``
   A time range, such as 08:00-14:00. Specified using a 24-hour clock in the
   timezone of the |TS| server.

.. _cache-config-format-src-ip:

``src_ip``
   Client IP address.

.. _cache-config-format-internal:

``internal``
    A boolean value, ``true`` or ``false``, specifying if the rule should
    match (or not match) a transaction originating from an internal API. This
    is useful to differentiate transactions originating from a |TS| plugin.

Actions
-------

The final component of a caching rule is the action, which determines what |TS|
will do with all objects matching the primary destinations and secondary
specifiers of the rule in question.

.. _cache-config-format-action:

``action``
   One of the following values:

   =========================== ================================================
   Value                       Effect
   =========================== ================================================
   ``never-cache``             Never cache specified objects, it will be
                               overwrited by ``ttl-in-cache``.
   ``ignore-no-cache``         Ignore all ``Cache-Control: no-cache`` headers.
   ``ignore-client-no-cache``  Ignore ``Cache-Control: no-cache`` headers from
                               client requests.
   ``ignore-server-no-cache``  Ignore ``Cache-Control: no-cache`` headers from
                               origin server responses.
   =========================== ================================================

.. _cache-responses-to-cookies:

``cache-responses-to-cookies``
   Change the style of caching with regard to cookies. This effectively
   overrides the configuration parameter
   :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`
   and uses the same values with the same semantics. The override happens
   only for requests that match.

.. _cache-config-format-pin-in-cache:

``pin-in-cache``
   Preserves objects in cache, preventing them from being overwritten.
   Does not affect objects that are determined not to be cacheable. This
   setting can have performance issues, and severely affect the cache.
   For instance, if the primary destination matches all objects, once the
   cache is full, no new objects could get written as nothing would be
   evicted. Similarly, for each cache-miss, each object would incur extra
   checks to determine if the object it would replace could be overwritten.

   The value is the amount of time you want to keep the object(s) in the cache.
   The following time formats are allowed:

   -  ``d`` for days; for example: 2d
   -  ``h`` for hours; for example: 10h
   -  ``m`` for minutes; for example: 5m
   -  ``s`` for seconds; for example: 20s
   -  mixed units; for example: 1h15m20s

.. _cache-config-format-revalidate:

``revalidate``
   For objects that are in cache, overrides the the amount of time the object(s)
   are to be considered fresh. Use the same time formats as ``pin-in-cache``.

.. _cache-config-format-ttl-in-cache:

``ttl-in-cache``
   Forces object(s) to become cached, as if they had a
   ``Cache-Control: max-age:<time>`` header. Can be overruled by requests with
   cookies. The value is the amount of time object(s) are to be kept in the
   cache, regardless of ``Cache-Control`` response headers from the origin
   server. Use the same time formats as ``pin-in-cache``.

Matching Multiple Rules
=======================

When multiple rules are specified in :file:`cache.config`, |TS| will check all
of them in order for each request. Thus, two rules which match the same request
but have conflicting actions will result in their actions being compounded. In
other words, |TS| does not stop on the first match.

In some cases, this may lead to confusing behavior. For example, consider the
following two rules::

    dest_domain=example.com prefix=foo suffix=js revalidate=7d
    dest_domain=example.com suffix=js action=never-cache

Reading that under the assumption that |TS| stops on the first match might lead
one to assume that all Javascript files will be excluded from the |TS| cache,
except for those whose paths begin with ``foo``. This, however, is not correct.
Instead, the first rule establishes that all Javascript files with the path
prefix ``foo`` will be forced to revalidate every seven days, and then the
second rule also sets an action on all Javascript files, regardless of their
path prefix, to never be cached at all. Because none of the Javascript files
will be cached at all, the first rule is effectively voided.

A similar example, but at least one with a correct solution, might be an
attempt to set differing values for the same action, as so::

    # Incorrect!
    dest_domain=example.com prefix=foo suffix=js revalidate=7d
    dest_domain=example.com suffix=js revalidate=1d

    # Correct!
    dest_domain=example.com suffix=js revalidate=1d
    dest_domain=example.com prefix=foo suffix=js revalidate=7d

The latter accomplishes the implied goal of having a default, or global, timer
for cache object revalidations on Javascript files, as well as a more targeted
(and longer) revalidation time on just those Javascript files with a particular
prefix. The former fails at this goal, because the second rule will match all
Javascript files and will override any previous ``revalidate`` values that may
have been set by prior rules.

ttl-in-cache and never-cache
----------------------------

When multiple rules are matched in the same request, ``never-cache`` will always
be overwrited by ``ttl-in-cache``. For example::

    # ttl-in-cache=1d never-cache=false
    dest_domain=example.com action=never-cache
    dest_domain=example.com ttl-in-cache=1d

Examples
========

The following example configures |TS| to revalidate ``gif`` and ``jpeg``
objects in the domain ``mydomain.com`` every 6 hours, and all other objects in
``mydomain.com`` every hour. The rules are applied in the order listed. ::

   dest_domain=mydomain.com revalidate=1h
   dest_domain=mydomain.com suffix=gif revalidate=6h
   dest_domain=mydomain.com suffix=jpeg revalidate=6h

Force a specific regex to be in cache between 7-11pm of the server's time for
26 hours. ::

   url_regex=example.com/articles/popular.* time=19:00-23:00 ttl-in-cache=1d2h

Prevent objects from being evicted from cache::

   url_regex=example.com/game/.* pin-in-cache=1h

