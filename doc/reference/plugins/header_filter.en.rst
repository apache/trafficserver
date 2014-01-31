.. _header-filter-plugin:

Header Filter Plugin
********************

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


.. deprecated:: 4.2.0
    Use :ref:`header-rewrite-plugin` instead, it provides the same, and more, functionality.


The ``header_filter`` is a simple plugin for filtering out headers from
requests (or responses). Typical configuration is done either with a
global configuration, in :file:`plugin.config`::

    header_filter.so /usr/local/etc/hdr_filters.conf

Or, alternatively, in a
```per-remap`` <../../configuration-files/remap.config>`_ rule
configuration ::

    map http://a.com/ http://b.com @plugin=header_filter.so @pparam=hdr_filters.conf

Even if you don't have a global configuration, if your remap rules
schedules actions in hooks other than during remap, you must also add
the ``header_filter.so`` to the
```plugin.config`` <../../configuration-files/remap.config>`_ (see
above), but without args::

    header_filter.so

The configuration files looks like ::

    [READ_REQUEST_HDR]
        X-From-Someone
        Cookie

    [READ_RESPONSE_HDR]
        X-From-Server
        Set-Cookie

    [SEND_RESPONSE_HDR]
        X-Fie "Test"    # Match the entire string
        X-Foo /Test/    # Match the (Perl) regex
        X-Bar [Test*    # Match the prefix string
        X-Fum *Test]    # Match the postfix string


Comments are prefixed with ``#``, and in most cases, the regular
expression matching is the best choice (very little overhead). The
pattern matches can also take an option '``!``\ ' to reverse the test.
The default action is to delete all headers that do (not) match the
pattern. E.g.::

    [SEND_REQUEST_HDR]
        X-Fie   /test/
        X-Foo ! /test/i

The final "``i``\ " qualifier (works on all pattern matches) forces the
match or comparison to be made case insensitive (just like in Perl).

It's also possible to replace or add headers, using the = and +
operators. For example ::

    [SEND_REQUEST_HDR]
        Host =www.example.com=
        X-Foo +ATS+

This will force the Host: header to have exactly one value,
``www.example.com``, while ``X-Foo`` will have at least one header with
the value ATS, but there could be more instances of the header from the
existing header in the request.

Possible hooks are ::

     READ_REQUEST_HDR
     SEND_REQUEST_HDR
     READ_RESPONSE_HDR
     SEND_RESPONSE_HDR

If not specified, the default hook to add the rules (headers to filter)
is ``READ_REQUEST_HDR``. It's completely acceptable (and useful) to
configure a remap rule to delete headers in a later hook (e.g. when
reading a response from the server). This is what actually makes the
plugin even remotely useful.


Examples
========

Set X-Forwarded-Proto https on SSL connections
----------------------------------------------

Often times a backend wants to know whether it's running under HTTP or
HTTPS. While not regulated standard, we can use the
``X-Forwarded-Proto`` header for this purpose.

In ```plugin.config`` <../../configuration-files/plugin.config>`_ we
need to add::

    header_filter.so

Then, in ```remap.config`` <../../configuration-files/remap.config>`_ we
can configure ``header_filter`` on a case by case basis::

    map http://example.org http://172.16.17.42:8080
    map https://example.org http://172.16.17.42:8080 @plugin=header_filter.so @pparam=/etc/trafficserver/x_fwd_proto.conf

The configuration that ties everything together is then
``/etc/trafficserver/x_fwd_proto.config``, to which we add::

    [SEND_REQUEST_HDR]
        X-Forwarded-Proto =https=

To activate this configuration, we need to restart Traffic Server with
:option:`traffic_line -L`.

In the backend servers we can now pick this up and do appropriately set
server variables that will be picked up by CGI programs for instance. In
the case of Apache httpd backend, this can be acomplished with
```mod_setenvif`` <http://httpd.apache.org/docs/current/mod/mod_setenvif.html#setenvif>`_::

    SetEnvIf X-Forwarded-Proto https HTTPS=on SSL=on
