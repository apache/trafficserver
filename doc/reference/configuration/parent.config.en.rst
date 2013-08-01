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
caching option is enabled (refer to `Configuring Traffic Server to Use a
Parent Cache <../hierachical-caching>`_).

After you modify the :file:`parent.config` file, run the :option:`traffic_line -x`
command to apply your changes. When you apply the changes to one node in
a cluster, Traffic Server automatically applies the changes to all other
nodes in the cluster.

Format
======

Each line in the :file:`parent.config` file must contain a parent caching
rule. Traffic Server recognizes three space-delimited tags:

    primary_destination=value secondary_specifier=value  action=value

The following list shows the possible primary destinations and their
allowed values.

*``dest_domain``* {#dest_domain}
    A requested domain name.

*``dest_host``* {#dest_host}
    A requested hostname.

*``dest_ip``* {#dest_ip}
    A requested IP address or range of IP addresses separated by a dash
    (-).

*``url_regex``* {#url_regex}
    A regular expression (regex) to be found in a URL

The secondary specifiers are optional in the :file:`parent.config` file. The
following list shows the possible secondary specifiers and their allowed
values.

*``port``* {#port}
    A requested URL port.

*``scheme``* {#scheme}
    A request URL protocol: ``http`` or ``https``.

*``prefix``* {#prefix}
    A prefix in the path part of a URL.

*``suffix``* {#suffix}
    A file suffix in the URL.

*``method``* {#method}
    A request URL method. It can be one of the following:

    -  get
    -  post
    -  put
    -  trace

*``time``* {#time}
    A time range, such as 08:00-14:00, during which the parent cache is
    used to serve requests.

*``src_ip``* {#src_ip}
    A client IP address.

The following list shows the possible actions and their allowed values.

``parent``
    An ordered list of parent servers. If the request cannot be handled
    by the last parent server in the list, then it will be routed to the
    origin server. You can specify either a hostname or an IP address,
    but; you must specify the port number.

``round_robin``
    One of the following values:

    -  ``true`` - Traffic Server goes through the parent cache list in a
       round robin-based on client IP address.
    -  ``strict`` - Traffic Server machines serve requests strictly in
       turn. For example: machine ``proxy1`` serves the first request,
       ``proxy2`` serves the second request, and so on.
    -  ``false`` - Round robin selection does not occur.

``go_direct``
    One of the following values:

    -  ``true`` - requests bypass parent hierarchies and go directly to
       the origin server.
    -  ``false`` - requests do not bypass parent hierarchies.

Examples
========

The following rule configures a parent cache hierarchy consisting of
Traffic Server (which is the child) and two parents, ``p1.x.com`` and
``p2.x.com``. Traffic Server forwards the requests it cannot serve to
the parent servers ``p1.x.com`` and ``p2.x.com`` in a round-robin
fashion::

    round_robin=true
    dest_domain=. method=get parent="p1.x.com:8080; p2.y.com:8080" round_robin=true

The following rule configures Traffic Server to route all requests
containing the regular expression ``politics`` and the path
``/viewpoint`` directly to the origin server (bypassing any parent
hierarchies): ``url_regex=politics prefix=/viewpoint go_direct=true``

Every line in the :file:`parent.config` file must contain either a
``parent=`` or ``go_direct=`` directive.

