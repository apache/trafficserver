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

=============
parent.config
=============

.. configfile:: parent.config

The :file:`parent.config` file identifies the parent proxies used in an
cache hierarchy. Use this file to perform the following configuration:

-  Set up parent cache hierarchies, with multiple parents and parent
   failover
-  Configure selected URL requests to bypass parent proxies

Traffic Server uses the :file:`parent.config` file only when the parent
caching option is enabled (refer to :ref:`configuring-traffic-server-to-use-a-parent-cache`).

After you modify the :file:`parent.config` file, run the :option:`traffic_ctl config reload`
command to apply your changes.

Format
======

Each line in the :file:`parent.config` file must contain a parent caching
rule. Traffic Server recognizes three space-delimited tags: ::

    primary_destination=value secondary_specifier=value  action=value

The following list shows the possible primary destinations and their
allowed values.

.. _parent-config-format-dest-domain:

``dest_domain``
    A requested domain name, and its subdomains.

.. _parent-config-format-dest-host:

``dest_host``
    A requested hostname.

.. _parent-config-format-dest-ip:

``dest_ip``
    A requested IP address or range of IP addresses separated by a dash (-).

.. _parent-config-format-url-regex:

``url_regex``
    A regular expression (regex) to be found in a URL

The secondary specifiers are optional in the :file:`parent.config` file. The
following list shows the possible secondary specifiers and their allowed
values. Every line in the :file:`parent.config` file must contain either a
``parent=`` or ``go_direct=`` directive.

.. _parent-config-format-port:

``port``
    A requested URL port.

.. _parent-config-format-scheme:

``scheme``
    A request URL protocol: ``http`` or ``https``.

.. _parent-config-format-prefix:

``prefix``
    A prefix in the path part of a URL.

.. _parent-config-format-suffix:

``suffix``
    A file suffix in the URL.

.. _parent-config-format-method:

``method``
    A request URL method. It can be one of the following:

    -  get
    -  post
    -  put
    -  trace

.. _parent-config-format-time:

``time``
    A time range, such as 08:00-14:00, during which the parent cache is
    used to serve requests.

.. _parent-config-format-src-ip:

``src_ip``
    A client IP address.

.. _parent-config-format-internal:

``internal``
    A boolean value, ``true`` or ``false``, specifying if the rule should
    match (or not match) a transaction originating from an internal API. This
    is useful to differentiate transaction originating from an ATS plugin.

The following list shows the possible actions and their allowed values.

.. _parent-config-format-parent:

``parent`` `(hostname or IP address):port[|weight][&hash name][,another host]`
    An ordered list of parent servers, separated by commas or
    semicolons. If the request cannot be handled by the last parent
    server in the list, then it will be routed to the origin server.
    You can specify either a hostname or an IP address, but, you must
    specify the port number. If there are multiple IP addresses associated
    with the hostname, |TS| will treat them as a single entity when tracking
    health. Example::

        parent="p1.x.com:8080, 192.168.0.3:80, 192.168.0.4:80"

    An optional weight can be specified after a pipe (``|``). This example
    has one parent take 20% (2/(2+3+5)) of the requests, another 30% (3/(2+3+5)),
    and the last 50% (5/(2+3+5))::

        parent="p1.x.com:8080|2.0, 192.168.0.3:80|3.0, 192.168.0.4:80|5.0"

    If ``round_robin`` is set to ``consistent_hash``, you may add a unique ``hash string``
    following the ``weight`` for each parent.  The ``hash string`` must start with ``&``
    and is used to build both the primary and secondary rings using the ``hash string``
    for each parent instead of the parents ``hostname`` or ``ip address``. This can be
    useful so that two different hosts may be used to cache the same requests.  Example::

        parent="p1.x.com:80|1.0&abcdef, p2.x.com:80|1.0&xyzl, p3.x.com:80|1.0&ldffg" round_robin=consistent_hash

.. _parent-config-format-secondary-parent:

``secondary_parent``
    An optional ordered list of secondary parent servers in the same format as parent.  This
    optional list may only be used when ``round_robin`` is set to
    ``consistent_hash``.  If the request cannot be handled by the
    first parent server chosen from the ``parent`` list, then the
    request will be re-tried from a server found in this list using a
    consistent hash of the url. The parent servers in this list will
    be exhausted before the selection function will revert to trying
    alternative parents in the ``parent`` list.

``secondary_mode``
    One of the following values:

    - ``1`` This is the default. The parent selection will first
      attempt to choose a parent from the ``parent`` list. If the
      chosen parent is not available or marked down then another
      parent will be chosen from the ``secondary_parent`` list.
      Choices in the ``secondary_parent`` list will be exhausted
      before attempting to choose another parent from the ``parent``
      list.

    - ``2`` The parent selection will first attempt to choose a parent
      from the ``parent`` list. If the chosen parent is not available
      or marked down then another parent will be chosen from the
      ``parent`` list.  Choices in the ``parent`` list will be
      exhausted before attempting to choose another parent from the
      ``secondary_parent`` list.

    - ``3`` The parent selection will first attempt to choose a parent
      from the ``parent`` list.

      - If the chosen parent is marked down then another parent will
        be chosen from the ``secondary_parent`` list. The
        ``secondary_parent`` list will be exhausted before attempting
        to choose another parent in the ``parent`` list. This depends
        on taking a parent down from a particular EDGE using traffic_ctl
        like ``traffic_ctl host down sample.server.com``. This will be
        useful during maintenance window or as a debugging aid when a
        user wants to take down specific parents. Taking parents down
        using ``traffic_ctl`` will cause the EDGE to ignore those parent
        immediately from parent selection logic.

      - If the chosen parent is unavailable but not marked down then
        another parent will be chosen from the ``parent`` list. The
        ``parent`` list will be exhausted before attempting to choose
        another parent in the ``secondary_parent`` list.

.. _parent-config-format-parent-is-proxy:

``parent_is_proxy``
    One of the following values:

    -  ``true`` - This is the default.  The list of parents and secondary parents
       are proxy cache servers.
    -  ``false`` - The list of parents and secondary parents are the origin
       servers ``go_direct`` flag is ignored and origins are selected using
       the specified ``round_robin`` algorithm.  The FQDN is removed from
       the http request line.

.. _parent-config-format-parent-retry:

``parent_retry``
    - ``simple_retry`` - If the parent returns a 404 response or if the response matches
      a list of http 4xx and/or 5xx responses defined in ``simple_server_retry_responses`` on a request
      a new parent is selected and the request is retried.  The number of retries is controlled
      by ``max_simple_retries`` which is set to 1 by default.
    - ``unavailable_server_retry`` - If the parent returns a 503 response or if the response matches
      a list of http 5xx responses defined in ``unavailable_server_retry_responses``, the currently selected
      parent is marked down and a new parent is selected to retry the request.  The number of
      retries is controlled by ``max_unavailable_server_retries`` which is set to 1 by default.
    - ``both`` - This enables both ``simple_retry`` and ``unavailable_server_retry`` as described above.
    - If not set, by default all response codes will be considered a success, and parents will not be retried based on any HTTP response code.

    .. Note::

        If a response code exists in both the simple and unavailable lists and both
        is the retry type then simple_retry will take precedence and unavailable_server_retry
        will not be used for that code.

.. _parent-config-format-simple-server-retry-responses:

``simple_server_retry_responses``
   If ``parent_retry`` is set to either ``simple_retry`` or ``both``, this parameter is a comma separated list of
   http 4xx and/or 5xx response codes that will invoke the ``simple_retry`` described in the ``parent_retry`` section. By
   default, ``simple_server_retry_responses`` is set to 404.

.. _parent-config-format-unavailable-server-retry-responses:

``unavailable_server_retry_responses``
  If ``parent_retry`` is set to either ``unavailable_server_retry`` or
  ``both``, this parameter is a comma separated list of http 5xx response codes that will invoke the
  ``unavailable_server_retry`` described in the ``parent_retry`` section.  By default, ``unavailable_server_retry_responses``
  is set to 503.

.. _parent-config-format-max-simple-retries:

``max_simple_retries``
  By default the value for ``max_simple_retries`` is 1.  It may be set to any value in the range 1 to 5.
  If ``parent_retry`` is set to ``simple_retry`` or ``both`` a 404 response
  from a parent origin server will cause the request to be retried using a new parent at most 1 to 5
  times as configured by ``max_simple_retries``.

.. _parent-config-format-max-unavailable-server-retries:

``max_unavailable_server_retries``
  By default the value for ``max_unavailable_server_retries`` is 1.  It may be set to any value in the range 1 to 5.
  If ``parent_retry`` is set to ``unavailable_server_retries`` or ``both`` a 503 response
  by default or any http 5xx response listed in the list ``unavailable_server_retry_responses`` from a parent origin server will
  cause the request to be retried using a new parent after first marking the current parent down.  The request
  will be retried at most 1 to 5 times as configured by ``max_unavailable_server_retries``.

.. _parent-config-format-round-robin:

``round_robin``
    One of the following values:

    -  ``true`` - Traffic Server determines the parent based on client IP address.
    -  ``strict`` - Traffic Server machines serve requests strictly in
       turn. For example: machine ``proxy1`` serves the first request,
       ``proxy2`` serves the second request, and so on.
    -  ``false`` - The default. Round robin selection does not occur.
    -  ``consistent_hash`` - consistent hash of the url so that one parent
       is chosen for a given url. If a parent is down, the traffic that
       would go to the down parent is rehashed amongst the remaining parents.
       The other traffic is unaffected. Once the downed parent becomes
       available, the traffic distribution returns to the pre-down
       state.
    - ``latched`` - The first parent in the list is marked as primary and is
      always chosen until connection errors cause it to be marked down.  When
      this occurs the next parent in the list then becomes primary.  The primary
      will wrap back to the first parent in the list when it is the last parent
      in the list and is marked down due to a connection error.  Newly chosen
      primary parents marked as unavailable will then be restored if the failure
      retry time has elapsed and the transaction using the primary succeeds.

.. _parent-config-format-go-direct:

``go_direct``
    One of the following values:

    -  ``true`` - The default. Requests bypass parent hierarchies and go directly to
       the origin server.

    -  ``false`` - requests do not bypass parent hierarchies.

.. _parent-config-format-qstring:

``qstring``
    One of the following values:

    -  ``consider`` - The default. Use the query string when finding a parent.

    -  ``ignore`` - Do not consider the query string when finding a parent. This
       is especially useful when using the ``consistent_hash`` selection strategy,
       and a random query string would prevent a consistent parent selection.

.. _parent-config-format-ignore_self_detect:

``ignore_self_detect``
    One of the following values:

  -  ``true`` - Ignore the marked down status of a host, typically the local host,
      when the reason code is Reason::SELF_DETECT and use the host as if it were
      marked up.

  -  ``false`` - The default.  Do not ignore the host status.

Examples
========

The following rule configures a parent cache hierarchy consisting of
Traffic Server (which is the child) and two parents, ``p1.x.com`` and
``p2.x.com``. Traffic Server forwards the requests it cannot serve to
the parent servers ``p1.x.com`` and ``p2.x.com`` in a round-robin
fashion::

    dest_domain=. method=get parent="p1.x.com:8080; p2.y.com:8080" round_robin=true

The following rule configures Traffic Server to route all requests
containing the regular expression ``politics`` and the path
``/viewpoint`` directly to the origin server (bypassing any parent
hierarchies)::

    url_regex=politics prefix=/viewpoint go_direct=true

The following configures Traffic Server to route http requests for example.com (neither
https nor www.example.com would match) through parent servers. Each url will be hashed
to a specific parent. If the chosen parent has been marked down, a parent from the
secondary ring will be chosen for the retry.::

    dest_host=example.com scheme=http parent="p1.x.com:80,p2.x.com:80" secondary_parent="p3.x.com:80,p4.x.com:80" round_robin=consistent_hash go_direct=false
