remap.config
************

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


.. toctree::                                                                                                                                                                                      
   :maxdepth: 2


The ``remap.config`` file contains mapping rules that Traffic Server
uses to perform the following actions:

-  Map URL requests for a specific origin server to the appropriate
   location on Traffic Server when Traffic Server acts as a reverse
   proxy for that particular origin server
-  Reverse-map server location headers so that when origin servers
   respond to a request with a location header that redirects the client
   to another location, the clients do not bypass Traffic Server
-  Redirect HTTP requests permanently or temporarily without Traffic
   Server having to contact any origin servers

Refer to `Reverse Proxy and HTTP
Redirects <../reverse-proxy-http-redirects>`_, for information about
redirecting HTTP requests and using reverse proxy.

**IMPORTANT:** After you modify the ``remap.config`` run the
``traffic_line -x`` command to apply the changes. When you apply the
changes to one node in a cluster, Traffic Server automatically applies
the changes to all other nodes in the cluster.

Format
======

Each line in the ``remap.config`` file must contain a mapping rule.
Traffic Server recognizes three space-delimited fields: ``type``,
``target``, and ``replacement``. The following list describes the format
of each field.

*``type``* {#type}
    Enter one of the following:

    -  ``map`` --translates an incoming request URL to the appropriate
       origin server URL.

    -  ``reverse_map`` --translates the URL in origin server redirect
       responses to point to the Traffic Server.

    -  ``redirect`` --redirects HTTP requests permanently without having
       to contact the origin server. Permanent redirects notify the
       browser of the URL change (by returning an HTTP status code 301)
       so that the browser can update bookmarks.

    -  ``redirect_temporary`` --redirects HTTP requests temporarily
       without having to contact the origin server. Temporary redirects
       notify the browser of the URL change for the current request only
       (by returning an HTTP status code 307).

    **Note:** use the ``regex_`` prefix to indicate that the line has a
    regular expression (regex).

*``target``* {#target}
    Enter the origin ("from") URL. You can enter up to four components:

    ::

        :::text
        scheme://host:port/path_prefix

    where *``scheme``* is ``http``.

*``replacement``* {#replacement}
    Enter the origin ("from") URL. You can enter up to four components:

    ::

        scheme://host:port/path_prefix

    where *``scheme``* can be ``http`` or ``https``.

Precedence
==========

Remap rules are not processed top-down, but based on an internal
priority

1. ``map`` and ``reverse_map``
2. ``redirect`` and ``redirect_temporary``
3. ``regex_remap``
4. ``regex_redirect`` and ``regex_redirect_temporary``

Examples
========

The following section shows example mapping rules in the
``remap.config`` file.

Reverse Proxy Mapping Rules
===========================

The following example shows a map rule that does not specify a path
prefix in the target or replacement:

::

    map http://www.x.com/ http://server.hoster.com/
    reverse_map http://server.hoster.com/ http://www.x.com/

This rule results in the following translations:

Client Request \| Translated Request
---------------\|-------------------
``http://www.x.com/Widgets/index.html`` \|
``http://server.hoster.com/Widgets/index.html``
``http://www.x.com/cgi/form/submit.sh?arg=true`` \|
``http://server.hoster.com/cgi/form/submit.sh?arg=true``

The following example shows a map rule with path prefixes specified in
the target:

::

    :::text
    map http://www.y.com/marketing/ http://marketing.y.com/
    reverse_map http://marketing.y.com/ http://www.y.com/marketing/
    map http://www.y.com/sales/ http://sales.y.com/
    reverse_map http://sales.y.com/ http://www.y.com/sales/
    map http://www.y.com/engineering/ http://engineering.y.com/
    reverse_map http://engineering.y.com/ http://www.y.com/engineering/
    map http://www.y.com/stuff/ http://info.y.com/
    reverse_map http://info.y.com/ http://www.y.com/stuff/

These rules result in the following translations:

Client Request \| Translated Request
---------------\|-------------------
``http://www.y.com/marketing/projects/manhattan/specs.html`` \|
``http://marketing.y.com/projects/manhattan/specs.html``
``http://www.y.com/stuff/marketing/projects/boston/specs.html`` \|
``http://info.y.com/marketing/projects/boston/specs.html``
``http://www.y.com/engineering/marketing/requirements.html`` \|
``http://engineering.y.com/marketing/requirements.html``

The following example shows that the order of the rules matters:

::

    :::text
    map http://www.g.com/ http://external.g.com/
    reverse_map http://external.g.com/ http://www.g.com/
    map http://www.g.com/stuff/ http://stuff.g.com/
    reverse_map http://stuff.g.com/ http://www.g.com/stuff/

These rules result in the following translation.

Client Request \| Translated Request
---------------\|------------------- ``http://www.g.com/stuff/a.gif`` \|
``http://external.g.com/stuff/a.gif``

In the above examples, the second rule is never applied because all URLs
that match the second rule also match the first rule. The first rule
takes precedence because it appears earlier in the ``remap.config``
file.

The following example shows a mapping with a path prefix specified in
the target and replacement:

::

    :::text
    map http://www.h.com/a/b/ http://server.h.com/customers/x/y
    reverse_map http://server.h.com/customers/x/y/ http://www.h.com/a/b/

This rule results in the following translation.

Client Request \| Translated Request
---------------\|-------------------
``http://www.h.com/a/b/c/d/doc.html`` \|
``http://server.h.com/customers/x/y/c/d/doc.html``
``http://www.h.com/a/index.html`` \| ``Translation fails``

The following example shows reverse-map rules:

::

    :::text
    map http://www.x.com/ http://server.hoster.com/x/
    reverse_map http://server.hoster.com/x/ http://www.x.com/

These rules result in the following translations.

Client Request \| Translated Request
---------------\|------------------- ``http://www.x.com/Widgets`` \|
``http://server.hoster.com/x/Widgets`` \|

 

Client Request \| Origin server Header \| Translated Header
---------------\|----------------------\|-------------------
``http://www.x.com/Widgets`` \| ``http://server.hoster.com/x/Widgets/``
\| ``http://www.x.com/Widgets/``

When acting as a reverse proxy for multiple servers, Traffic Server is
unable to route to URLs from older browsers that do not send the
``Host:`` header. As a solution, set the variable
*``proxy.config.header.parse.no_host_url_redirect``* in the
:file:`records.config` file to the URL to which Traffic Server will redirect
requests without host headers.

Redirect Mapping Rules
======================

The following rule permanently redirects all HTTP requests for
``www.company.com`` to ``www.company2.com``:

::

    redirect http://www.company.com/ http://www.company2.com/

The following rule *temporarily* redirects all HTTP requests for
``www.company1.com`` to ``www.company2.com``:

::

    redirect_temporary http://www.company1.com/ http://www.company2.com/

Regular Expression (regex) Remap Support
========================================

Regular expressions can be specified in remapping rules, with the
limitations below:

-  Only the ``host`` field can contain a regex; the ``scheme``,
   ``port``, and other fields cannot. For path manipulation via regexes,
   use the ``regex_remap`` plugin.
-  The number of capturing subpatterns is limited to 9. This means that
   ``$0`` through ``$9`` can be used as subtraction placeholders (``$0``
   will be the entire input string).
-  The number of substitutions in the expansion string is limited to 10.
-  There is no ``regex_`` equivalent to ``reverse_remap``, so when using
   ``regex_remap`` you should make sure the reverse path is clear by
   setting
   (`*``proxy.config.url_remap.pristine_host_hdr``* <../configuration-files/records.config#proxy.config.url_remap.pristine_host_hdr>`_)

Examples
--------

::

    regex_map http://x([0-9]+).z.com/ http://real-x$1.z.com/
    regex_redirect http://old.(.*).z.com http://new.$1.z.com

Plugin Chaining
===============

Plugins can be configured to be evaluated in a specific order, passing
the results from one in to the next (unless a plugin returns 0, then the
"chain" is broken).

Examples
--------

::

    map http://url/path http://url/path @plugin=/etc/traffic_server/config/plugins/plugin1.so @pparam=1 @pparam=2 @plugin=/etc/traffic_server/config/plugins/plugin2.so @pparam=3

will pass "1" and "2" to plugin1.so and "3" to plugin2.so.

This will pass "1" and "2" to plugin1.so and "3" to plugin2.so
