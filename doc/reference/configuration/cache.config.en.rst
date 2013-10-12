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

.. configfile:: cache.config

============
cache.config
============

The :file:`cache.config` file defines how Traffic Server caches web objects. You
can add caching rules to specify the following: - Not to cache objects
from specific IP addresses - How long to pin particular objects in the
cache - How long to consider cached objects as fresh - Whether to ignore
no-cache directives from the server

.. important::

   After you modify the :file:`cache.config` file, navigate to
   the Traffic Server bin directory; then run the :option:`traffic_line -x`
   command to apply changes. When you apply the changes to a node in a
   cluster, Traffic Server automatically applies the changes to all other
   nodes in the cluster.

Format
======

Each line in the :file:`cache.config` file contains a caching rule. Traffic
Server recognizes three space-delimited tags::

   primary_destination=value secondary_specifier=value action=value

You can use more than one secondary specifier in a rule. However, you
cannot repeat a secondary specifier. The following list shows the
possible primary destinations with allowed values.

.. _cache-config-format-dest-domain:

``dest_domain``
   A requested domain name. Traffic Server matches the domain name of
   the destination from the URL in the request.

.. _cache-config-format-dest-host:

``dest_host``
   A requested hostname. Traffic Server matches the hostname of the
   destination from the URL in the request.

.. _cache-config-format-dest-ip:

``dest_ip``
   A requested IP address. Traffic Server matches the IP address of the
   destination in the request.

.. _cache-config-format-url-regex:

``url_regex``
   A regular expression (regex) to be found in a URL.

The secondary specifiers are optional in the :file:`cache.config` file. The
following list shows possible secondary specifiers with allowed values.

.. _cache-config-format-port:

``port``
   A requested URL port.

.. _cache-config-format-scheme:

``scheme``
   A request URL protocol: http or https.

.. _cache-config-format-prefix:

``prefix``
   A prefix in the path part of a URL.

.. _cache-config-format-suffix:

``suffix``
   A file suffix in the URL.

.. _cache-config-format-method:

``method``
   A request URL method: get, put, post, trace.

.. _cache-config-format-time:

``time``
   A time range, such as 08:00-14:00.

.. _cache-config-format-src-ip:

``src_ip``
   A client IP address.

The following list shows possible actions and their allowed values.


.. _cache-config-format-action:

``action``
   One of the following values:

   -  ``never-cache`` configures Traffic Server to never cache
      specified objects.
   -  ``ignore-no-cache`` configures Traffic Server to ignore all
      ``Cache-Control: no-cache`` headers.
   -  ``ignore-client-no-cache`` configures Traffic Server to ignore
      ``Cache-Control: no-cache`` headers from client requests.
   -  ``ignore-server-no-cache`` configures Traffic Server to ignore
      ``Cache-Control: no-cache`` headers from origin server responses.
   -  ``cluster-cache-local`` configures the cluster cache to allow for
      this content to be stored locally on every cluster node.


.. _cache-config-format-pin-in-cache:

``pin-in-cache``
   The amount of time you want to keep the object(s) in the cache. The
   following time formats are allowed:

   -  ``d`` for days; for example: 2d
   -  ``h`` for hours; for example: 10h
   -  ``m`` for minutes; for example: 5m
   -  ``s`` for seconds; for example: 20s
   -  mixed units; for example: 1h15m20s

.. _cache-config-format-revalidate:

``revalidate``
   The amount of time object(s) are to be considered fresh. Use the
   same time formats as ``pin-in-cache``.

.. _cache-config-format-ttl-in-cache:

``ttl-in-cache``
   The amount of time object(s) are to be kept in the cache, regardless
   of Cache-Control response headers. Use the same time formats as
   pin-in-cache and revalidate .

Examples
========

The following example configures Traffic Server to revalidate ``gif``
and ``jpeg`` objects in the domain ``mydomain.com`` every 6 hours, and
all other objects in ``mydomain.com`` every hour. The rules are applied
in the order listed. ::

   dest_domain=mydomain.com suffix=gif revalidate=6h
   dest_domain=mydomain.com suffix=jpeg revalidate=6h
   dest_domain=mydomain.com revalidate=1h

