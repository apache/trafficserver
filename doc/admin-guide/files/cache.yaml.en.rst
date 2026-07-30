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

.. configfile:: cache.yaml

cache.yaml
**********

The :file:`cache.yaml` file allows administrators to override origin cache
policies for selected requests. It replaces the legacy :file:`cache.config`
format. After modifying the file, run :option:`traffic_ctl config reload` to
apply the changes.

Generally, origin-provided ``Cache-Control`` headers are preferable because the
origin can make finer-grained decisions. Use :file:`cache.yaml` for policies
that must be enforced by the proxy.

Format
======

The top-level ``cache`` key contains a sequence of rules. Each rule has an
optional ``match`` map and a required ``action`` map:

.. code-block:: yaml

   cache:
     - match:
         dest_domain: example.com
         suffix: js
       action:
         revalidate: 6h
         ignore_server_no_cache: true

     - match:
         dest_domain: example.com
       action:
         revalidate: 1h

A rule without a ``match`` map matches every request.

Matching
========

A ``match`` map may contain at most one primary match key:

================= ============================================================
Key               Meaning
================= ============================================================
``dest_host``     Exact destination host name.
``dest_domain``   Destination domain name.
``dest_ip``       Destination IP address or range.
``url_regex``     Regular expression matched against the request URL.
``host_regex``    Regular expression matched against the destination host.
================= ============================================================

It may also contain any of these secondary match keys:

================= ============================================================
Key               Meaning
================= ============================================================
``port``          Request URL port or port range.
``scheme``        Request URL scheme, such as ``http`` or ``https``.
``prefix``        Prefix of the URL path.
``suffix``        Suffix of the URL path. Comma-separated values are allowed.
``method``        HTTP request method.
``time``          Server-local 24-hour time range, such as ``08:00-14:00``.
``src_ip``        Client IP address or range.
``incoming_port`` Local port on which the request was received.
``tag``           Tag supplied by an internal caller.
``internal``      Whether the transaction originated from an internal API.
================= ============================================================

First Matching Rule
-------------------

Rules are evaluated in the order listed, and only the first matching rule is
applied. Actions from later rules are not combined with the selected rule.
Place specific rules before general rules:

.. code-block:: yaml

   cache:
     - match:
         dest_domain: example.com
         suffix: jpeg
       action:
         revalidate: 6h

     - match:
         dest_domain: example.com
       action:
         revalidate: 1h

Here JPEG objects use a six-hour revalidation interval, while other objects in
the domain use one hour.

.. _cache-yaml-actions:

Actions
========

The ``action`` map supports the following keys:

============================== ===============================================
Key                            Meaning
============================== ===============================================
``cache``                      ``never`` prevents caching; ``standard`` uses
                               normal cacheability rules.
``revalidate``                 How long matching cached objects remain fresh.
``pin_in_cache``               How long matching objects are protected from
                               eviction.
``ttl_in_cache``               Forces matching objects into cache for the
                               specified duration.
``ignore_no_cache``            Ignores client and server no-cache directives.
``ignore_client_no_cache``     Ignores client no-cache directives.
``ignore_server_no_cache``     Ignores origin no-cache directives.
``cache_responses_to_cookies`` Overrides
                               :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`
                               with an integer from 0 through 4.
============================== ===============================================

Durations accept days, hours, minutes, and seconds, including mixed values such
as ``1d2h`` or ``15m20s``. A rule cannot combine ``cache: never`` with
``ttl_in_cache`` because the two actions conflict.

Multiple actions may be set by one rule:

.. code-block:: yaml

   cache:
     - match:
         dest_domain: example.com
         prefix: /assets/
       action:
         ttl_in_cache: 1d
         pin_in_cache: 2h
         cache_responses_to_cookies: 0

Migration from cache.config
===========================

Use :option:`traffic_ctl config convert` to convert an existing
:file:`cache.config` file:

.. code-block:: bash

   traffic_ctl config convert cache cache.config cache.yaml

Use ``-`` as the output file to preview the conversion on standard output:

.. code-block:: bash

   traffic_ctl config convert cache cache.config -

The converter preserves rule order and combines the directive and any tweaks
from each legacy line into one YAML action map. Review overlapping rules before
deploying the result: legacy :file:`cache.config` rules accumulate actions from
every match, whereas :file:`cache.yaml` applies only the first matching rule.

The line-based parser remains available during migration. Set
``proxy.config.cache.control.filename`` to ``cache.config`` to use it.
