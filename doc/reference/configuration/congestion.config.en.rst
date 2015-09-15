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

=================
congestion.config
=================

.. configfile:: congestion.config

The :file:`congestion.config` file (by default, located in 
``/usr/local/etc/trafficserver/``) enables you to configure Traffic Server
to stop forwarding HTTP requests to origin servers when they become
congested, and then send the client a message to retry the congested
origin server later. After you modify the :file:`congestion.config` file,
navigate to the Traffic Server bin directory; then run the
:option:`traffic_ctl config reload` command to apply changes. When you apply the changes
to a node in a cluster, Traffic Server automatically applies the changes
to all other nodes in the cluster. Traffic Server uses the
:file:`congestion.config` file only if you enable the 
:ts:cv:`proxy.config.http.congestion_control.enabled` option.

You can create rules in the congestion.config file to specify:

-  Which origin servers Traffic Server tracks for congestion.
-  The timeouts Traffic Server uses, depending on whether a server is
   congested.
-  The page Traffic Server sends to the client when a server becomes
   congested.
-  If Traffic Server tracks the origin servers per IP address or per
   hostname.

Format
======

Each line in :file:`congestion.config` must follow the format below. Traffic
Server applies the rules in the order listed, starting at the top of the
file. Traffic Server recognizes three space-delimited tags::

    primary_destination=value secondary_specifier=value action=value

The following list shows possible primary destinations with allowed
values.

``dest_domain``
    A requested domain name.

``dest_host``
    A requested hostname.

``dest_ip``
    A requested IP address.

``url_regex``
    A regular expression (regex) to be found in a URL.

The secondary specifiers are optional in the congestion.config file. The
following list shows possible secondary specifiers with allowed values.
You can use more than one secondary specifier in a rule; however, you
cannot repeat a secondary specifier.

``port``
    A requested URL port or range of ports.

``prefix``
    A prefix in the path part of a URL.

The following list shows the possible tags and their allowed values.

``max_connection_failures``
    Default: ``5``
    The maximum number of connection failures allowed within the fail
    window described below before Traffic Server marks the origin server
    as congested.

``fail_window``
    Default: ``120`` seconds.
    The time period during which the maximum number of connection
    failures can occur before Traffic Server marks the origin server as
    congested.

``proxy_retry_interval``
    Default: ``10`` seconds.
    The number of seconds that Traffic Server waits before contacting a
    congested origin server again.

``client_wait_interval``
    Default: ``300`` seconds.
    The number of seconds that the client is advised to wait before
    retrying the congested origin server.

``wait_interval_alpha``
    Default: ``30`` seconds
    The upper limit for a random number that is added to the wait
    interval.

``live_os_conn_timeout``
    Default: ``60`` seconds.
    The connection timeout to the live (uncongested) origin server. If a
    client stops a request before the timeout occurs, then Traffic
    Server does not record a connection failure.

``live_os_conn_retries``
    Default: ``2``
    The maximum number of retries allowed to the live (uncongested)
    origin server.

``dead_os_conn_timeout``
    Default: ``15`` seconds.
    The connection timeout to the congested origin server.

``dead_os_conn_retries``
    Default: ``1``
    The maximum number of retries allowed to the congested origin
    server.

``max_connection``
    Default: ``-1``
    The maximum number of connections allowed from Traffic Server to the
    origin server.

``error_page``
    Default: ``"congestion#retryAfter"``
    The error page sent to the client when a server is congested. You
    must enclose the value in quotes;

``congestion_scheme``
    Default: ``"per_ip"``
    Specifies if Traffic Server applies the rule on a per-host
    (``"per_host"``) or per-IP basis (``"per_ip"``). You must enclose
    the value in quotes.

    For example: if the server ``www.host1.com`` has two IP addresses
    and you use the tag value ``"per_ip"``, then each IP address has its
    own number of connection failures and is marked as congested
    independently. If you use the tag value ``"per_host"`` and the
    server ``www.host1.com`` is marked as congested, then both IP
    addresses are marked as congested.

Examples
========

The following :file:`congestion.config` rule configures Traffic Server to
stop forwarding requests to the server ``www.host.com`` on port 80 (HTTP
traffic) if the server is congested, according to the timeouts
specified. Traffic Server uses the default tag values because no tag has
been specified.

::

    dest_host=www.host.com port=80

You can use one or more tags in a rule, but each tag must have one value
only. If you specify no tags in the rule, then Traffic Server uses the
default values.

You can override any of the default tag values by adding configuration
variables at the end of :file:`records.config` as follows:

::

    CONFIG proxy.config.http.congestion_control.default.tag INT|STRING value

where tag is one of the tags described in the list under
:file:`congestion.config` and value is the value you
want to use.

For example::

    CONFIG proxy.config.http.congestion_control.default.congestion_scheme STRING per_host

.. important::

    Rules in the :file:`congestion.config` file override the
    following variables in the :file:`records.config` file:

::

    proxy.config.http.connect_attempts_max_retries
    proxy.config.http.connect_attempts_max_retries_dead_server
    proxy.config.http.connect_attempts_rr_retries
    proxy.config.http.connect_attempts_timeout
    proxy.config.http.down_server.cache_time
    proxy.config.http.down_server.abort_threshold

