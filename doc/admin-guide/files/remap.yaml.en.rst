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

===========
remap.yaml
===========

.. configfile:: remap.yaml

.. include:: ../../common.defs

.. toctree::
   :maxdepth: 2


The :file:`remap.yaml` file (by default, located in
``/usr/local/etc/trafficserver/``) provides a YAML-based alternative to
:file:`remap.config` for configuring URL remapping rules. Traffic Server
uses these mapping rules to perform the following actions:

-  Map URL requests for a specific origin server to the appropriate
   location on Traffic Server when Traffic Server acts as a reverse
   proxy for that particular origin server
-  Reverse-map server location headers so that when origin servers
   respond to a request with a location header that redirects the client
   to another location, the clients do not bypass Traffic Server
-  Redirect HTTP requests permanently or temporarily without Traffic
   Server having to contact any origin servers

Refer to  :ref:`reverse-proxy-and-http-redirects`, for information about
redirecting HTTP requests and using reverse proxy.

After you modify the :file:`remap.yaml` run the
:option:`traffic_ctl config reload` to apply the changes. The current configuration is replaced
with the new configuration only if there are no errors in the file. Any syntax error will prevent
an update. Even if syntactically correct the file is considered valid only if it has at least :ts:cv:`proxy.config.url_remap.min_rules_required`
rules in it. This defaults to 0, but can be set higher if it is desirable to prevent loading an
empty or missing file.

Configuration File Fallback
============================

Traffic Server will attempt to load :file:`remap.yaml` first. If this file is not found,
it will fall back to loading :file:`remap.config`. If both files exist, only
:file:`remap.yaml` will be used. This allows for a gradual migration from the legacy
configuration format to YAML.

Format
======

The :file:`remap.yaml` file uses YAML syntax with two main sections:

- ``acl_filters`` (optional): Global named filter definitions
- ``remap`` (required): Sequence of remapping rules

Basic Structure
---------------

.. code-block:: yaml

   # Optional: Global named filter definitions
   acl_filters:
     filter_name:
       # filter definition

   # Required: Remapping rules
   remap:
     - type: map
       from:
         url: http://example.com/foo
       to:
         url: http://backend.com/bar
     - type: redirect
       from:
         url: old.example.com
       to:
         url: new.example.com

Comments and Empty Lines
-------------------------

Lines starting with ``#`` are comments and are ignored. Empty lines are also ignored.
Unlike :file:`remap.config`, YAML does not support line continuation with ``\``.

Rule Structure
==============

Each remap rule is a YAML mapping with the following fields:

``type``
--------

Specifies the type of remapping rule. Required field. One of:

-  ``map`` -- translates an incoming request URL to the appropriate
   origin server URL.

-  ``map_with_recv_port`` -- exactly like 'map' except that it uses the port at
   which the request was received to perform the mapping instead of the port present
   in the request. The ``regex_`` prefix can also be used for this type. When present,
   'map_with_recv_port' mappings are checked first.

-  ``map_with_referer`` -- extended version of 'map', which can be used to activate
   "deep linking protection", where target URLs are only accessible when the Referer
   header is set to a URL that is allowed to link to the target.

-  ``reverse_map`` -- translates the URL in origin server redirect
   responses to point to the Traffic Server.

-  ``redirect`` -- redirects HTTP requests permanently without having
   to contact the origin server. Permanent redirects notify the
   browser of the URL change (by returning an HTTP status code 301)
   so that the browser can update bookmarks.

-  ``redirect_temporary`` -- redirects HTTP requests temporarily
   without having to contact the origin server. Temporary redirects
   notify the browser of the URL change for the current request only
   (by returning an HTTP status code 307).

.. note:: use the ``regex_`` prefix for the type to indicate that the rule uses regular expressions.

``from``
--------

Specifies the request ("from") URL as a YAML mapping. URL can be defined as a single URL or in components.
Single URL takes precedence over components:

- ``url``: scheme://host:port/path

URL components:

-  ``scheme`` (optional): ``http``, ``https``, ``ws``, ``wss``, or ``tunnel``.
-  ``host`` (optional for forward maps with path, required otherwise): Hostname or IP address
-  ``port`` (optional): Port number
-  ``path`` (optional): Path prefix

Example:

.. code-block:: yaml

   from:
     url: http://example.com:80/foo
   from:
     scheme: http
     host: example.com
     port: 80
     path: /foo

.. note:: A remap rule for requests that upgrade from HTTP to WebSocket still require a remap rule with the ``ws`` or ``wss`` scheme.

``to``
------

Specifies the origin ("to") URL as a YAML mapping. Required field.

URL components are the same as ``from``.

Example:

.. code-block:: yaml

   to:
     url: http://backend.example.com:8080/bar
   to:
     scheme: http
     host: backend.example.com
     port: 8080
     path: /bar

Optional Fields
---------------

``acl_filter``
~~~~~~~~~~

Inline ACL filter definition for a single remap rule. See `ACL Filters`_ for details.

.. code-block:: yaml

   acl_filter:
     src_ip:
      - 10.0.0.0/8
     method:
      - GET
      - POST
     action: allow

``plugins``
~~~~~~~~~~~

Sequence of plugin configurations. Each plugin has:

-  ``name`` (required): Plugin filename
-  ``params`` (optional): List of plugin parameters

.. code-block:: yaml

   plugins:
     - name: header_rewrite.so
       params:
        - config.txt
        - param2
     - name: another_plugin.so
       params:
        - param1

``strategy``
~~~~~~~~~~~~

NextHop selection strategy name. See :doc:`../configuration/hierarchical-caching.en` and :doc:`strategies.yaml.en`.

.. code-block:: yaml

   strategy: my_strategy

``redirect``
~~~~~~~~~~~~

Used with ``map_with_referer`` type. Specifies redirect URL and allowed referer patterns.

.. code-block:: yaml

   redirect:
     url: http://example.com/denied  # or "default" for default redirect URL
     regex:
       - "~https://trusted\\.com/.*"  # regex pattern
       - "~"  # allow any referer (makes referer optional)

Precedence
==========

Remap rules in :file:`remap.yaml` follow the same precedence order as :file:`remap.config`:

1. ``map_with_recv_port`` and ``regex_map_with_recv_port``
#. ``map`` and ``regex_map`` and ``reverse_map``
#. ``redirect`` and ``redirect_temporary``
#. ``regex_redirect`` and ``regex_redirect_temporary``

For each precedence group the rules are checked in two phases. If the first phase fails to find a
match then the second phase is performed against the same group of rules. In the first phase the
rules are checked using the host name of the request. Only rules that specify a host name can match.
If there is no match in that phase, then the rules are checked again with no host name and only
rules without a host will match. The result is that rules with an explicit host take precedence over
rules without.

Match-All
=========

A map rule with only a path of ``/`` acts as a wildcard, it will match any
request. This should be use with care, and certainly only once at the
end of the remap section. E.g.

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: /
       to:
         url: http://all.example.com

:file:`remap.config` equivalent ::

    map / http://all.example.com

Examples
========

The following sections show example mapping rules in the :file:`remap.yaml` file.

Reverse Proxy Mapping Rules
----------------------------

The following example shows a map rule that does not specify a path
prefix in the target or replacement:

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://www.x.com/
       to:
         url: http://server.hoster.com/
     - type: reverse_map
       from:
         url: http://server.hoster.com/
       to:
         url: http://www.x.com/

:file:`remap.config` equivalent ::

    map http://www.x.com/ http://server.hoster.com/
    reverse_map http://server.hoster.com/ http://www.x.com/

This rule results in the following translations:

================================================ ========================================================
Client Request                                   Translated Request
================================================ ========================================================
``http://www.x.com/Widgets/index.html``          ``http://server.hoster.com/Widgets/index.html``
``http://www.x.com/cgi/form/submit.sh?arg=true`` ``http://server.hoster.com/cgi/form/submit.sh?arg=true``
================================================ ========================================================

The following example shows a map rule with path prefixes specified in
the target:

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://www.y.com/marketing/
       to:
         url: http://marketing.y.com/
     - type: reverse_map
       from:
         url: http://marketing.y.com/
       to:
         url: http://www.y.com/marketing/
     - type: map
       from:
         url: http://www.y.com/sales/
       to:
         url: http://sales.y.com/
     - type: reverse_map
       from:
         url: http://sales.y.com/
       to:
         url: http://www.y.com/sales/
    - type: map
       from:
         url: http://www.y.com/engineering/
       to:
         url: http://engineering.y.com/
     - type: reverse_map
       from:
         url: http://engineering.y.com/
       to:
         url: http://www.y.com/engineering/
     - type: map
       from:
         url: http://www.y.com/stuff/
       to:
         url: http://info.y.com/
     - type: reverse_map
       from:
         url: http://info.y.com/
       to:
         url: http://www.y.com/stuff/

:file:`remap.config` equivalent ::

    map http://www.y.com/marketing/ http://marketing.y.com/
    reverse_map http://marketing.y.com/ http://www.y.com/marketing/
    map http://www.y.com/sales/ http://sales.y.com/
    reverse_map http://sales.y.com/ http://www.y.com/sales/
    map http://www.y.com/engineering/ http://engineering.y.com/
    reverse_map http://engineering.y.com/ http://www.y.com/engineering/
    map http://www.y.com/stuff/ http://info.y.com/
    reverse_map http://info.y.com/ http://www.y.com/stuff/

These rules result in the following translations:

=============================================================== ==========================================================
Client Request                                                  Translated Request
=============================================================== ==========================================================
``http://www.y.com/marketing/projects/manhattan/specs.html``    ``http://marketing.y.com/projects/manhattan/specs.html``
``http://www.y.com/stuff/marketing/projects/boston/specs.html`` ``http://info.y.com/marketing/projects/boston/specs.html``
=============================================================== ==========================================================

The following example shows that the order of the rules matters:

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://www.g.com/
       to:
         url: http://external.g.com/
     - type: reverse_map
       from:
         url: http://external.g.com/
       to:
         url: http://www.g.com/
     - type: map
       from:
         url: http://www.g.com/stuff/
       to:
         url: http://stuff.g.com/
     - type: reverse_map
       from:
         url: http://stuff.g.com/
       to:
         url: http://www.g.com/stuff/

:file:`remap.config` equivalent ::

    map http://www.g.com/ http://external.g.com/
    reverse_map http://external.g.com/ http://www.g.com/
    map http://www.g.com/stuff/ http://stuff.g.com/
    reverse_map http://stuff.g.com/ http://www.g.com/stuff/

These rules result in the following translation.

================================ =====================================
Client Request                   Translated Request
================================ =====================================
``http://www.g.com/stuff/a.gif`` ``http://external.g.com/stuff/a.gif``
================================ =====================================

In the above examples, the second rule is never applied because all URLs
that match the second rule also match the first rule. The first rule
takes precedence because it appears earlier in the :file:`remap.config`
file.

This is different if one rule does not have a host. For example consider these rules using the `Match-All`_ rule

.. code-block:: yaml

  remap:
    - type: map
      from:
        url: /
      to:
        url: http://127.0.0.1:8001/
    - type: map
      from:
        url: http://example.com/dist_get_user
      to:
        url: http://127.0.0.1:8001/denied.html

These rules are set up to redirect requests to another local process. Using them will result in

==================================== =====================================
Client Request                       Translated Request
==================================== =====================================
``http://example.com/a.gif``         ``http://127.0.0.1:8001/a.gif``
``http://example.com/dist_get_user`` ``http://127.0.0.1:8001/denied.html``
==================================== =====================================

For the first request the second rule host matches but the path does not and so the second rule is
not selected. The first rule is then matched in the second phase when the rules are checked without
a host value.

The second request is matched by the second rule even though the rules have the same base
precedence. Because the first rule does not have a host it will not match in the first phase. The
second rule does have a host that matches the host in the second request along with the other parts
of the URL and is therefore selected in the first phase.

This will yield the same results if the rules are reversed because the rule selection happens in
different phases making the order irrelevant.

.. code-block:: yaml

  remap:
    - type: map
      from:
        url: http://example.com/dist_get_user
      to:
        url: http://127.0.0.1:8001/denied.html
    - type: map
      from:
        url: /
      to:
        url: http://127.0.0.1:8001/

The following example shows a mapping with a path prefix specified in
the target and replacement

.. code-block:: yaml

  remap:
    - type: map
      from:
        url: http://www.h.com/a/b/
      to:
        url: http://server.h.com/customers/x/y
    - type: reverse_map
      from:
        url: http://server.h.com/customers/x/y/
      to:
        url: http://www.h.com/a/b/

This rule results in the following translation.

===================================== ==================================================
Client Request                        Translated Request
===================================== ==================================================
``http://www.h.com/a/b/c/d/doc.html`` ``http://server.h.com/customers/x/y/c/d/doc.html``
``http://www.h.com/a/index.html``     ``Translation fails``
===================================== ==================================================

The following example shows reverse-map rules

.. code-block:: yaml

  remap:
    - type: map
      from:
        url: www.x.com
      to:
        url: http://server.hoster.com/x/
    - type: reverse_map
      from:
        url: http://server.hoster.com/x/
      to:
        url: http://www.x.com/

These rules result in the following translations.

================================ =====================================
Client Request                   Translated Request
================================ =====================================
``http://www.x.com/Widgets``     ``http://server.hoster.com/x/Widgets``
================================ =====================================



================================ ======================================= =============================
Client Request                   Origin Server Header                    Translated Request
================================ ======================================= =============================
``http://www.x.com/Widgets``     ``http://server.hoster.com/x/Widgets/`` ``http://www.x.com/Widgets/``
================================ ======================================= =============================

When acting as a reverse proxy for multiple servers, Traffic Server is
unable to route to URLs from older browsers that do not send the
``Host:`` header. As a solution, set the variable :ts:cv:`proxy.config.header.parse.no_host_url_redirect`
in the :file:`records.yaml` file to the URL to which Traffic Server will redirect
requests without host headers.


Redirect Mapping Rules
-----------------------

The following rule permanently redirects all HTTP requests for
``www.company.com`` to ``www.company2.com``:

.. code-block:: yaml

   remap:
     - type: redirect
       from:
         url: http://www.company.com/
       to:
         url: http://www.company2.com/

The following rule *temporarily* redirects all HTTP requests for
``www.company1.com`` to ``www.company2.com``:

.. code-block:: yaml

   remap:
     - type: redirect_temporary
       from:
         url: http://www.company1.com/
       to:
         url: http://www.company2.com/

Regular Expression (regex) Remap Support
=========================================

Regular expressions can be specified in remapping rules by using the ``regex_`` prefix
for the rule type, with the same limitations as :file:`remap.config`:

-  Only the ``host`` field can contain a regex; the ``scheme``,
   ``port``, and other fields cannot. For path manipulation via regexes,
   use the :ref:`admin-plugins-regex-remap`.
-  The number of capturing subpatterns is limited to 9. This means that
   ``$0`` through ``$9`` can be used as substitution placeholders (``$0``
   will be the entire input string).
-  The number of substitutions in the expansion string is limited to 10.
-  There is no ``regex_`` equivalent to ``reverse_remap``, so when using
   ``regex_map`` you should make sure the reverse path is clear by
   setting (:ts:cv:`proxy.config.url_remap.pristine_host_hdr`)

Examples
--------

.. code-block:: yaml

   remap:
     - type: regex_map
       from:
         url: http://x([0-9]+).z.com/
       to:
         url: http://real-x$1.z.com/
     - type: regex_redirect
       from:
         url: http://old.(.*).z.com
       to:
         url: http://new.$1.z.com

map_with_recv_port
==================

The ``map_with_recv_port`` type supports two special URL schemes, ``http+unix`` and ``https+unix``.
These are useful if you want to have different mapping rules or different plugin configuration for requests received via Unix Domain Socket.

Examples
--------

.. code-block:: yaml

   remap:
     - type: map_with_recv_port
       from:
         url: http://foo.example.com:8000/
       to:
         url: http://x.example.com/
     - type: map_with_recv_port
       from:
         url: http://foo.example.com:8888/
       to:
         url: http://y.example.com/

Explanation: Requests received on port 8000 and 8888 are forwarded to different servers.

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://x.example.com/
       plugins:
         - name: plugin1.so
     - type: map_with_recv_port
       from:
         url: http+unix://foo.example.com/
       to:
         url: http://x.example.com/

Explanation: All requests are forwarded to the same server, but plugin1 does not run for requests received via Unix Domain Socket.

map_with_referer
================

'redirect-URL' is a redirection URL specified according to RFC 2616 and can
contain special formatting instructions for run-time modifications of the
resulting redirection URL.  All regexes Perl compatible  regular expressions,
which describes the content of the "Referer" header which must be
verified. In case an actual request does not have "Referer" header or it
does not match with referer regular expression, the HTTP request will be
redirected to 'redirect-URL'.

The ``map_with_referer`` type enables "deep linking protection" by validating
the Referer header against regular expressions.

.. code-block:: yaml

   remap:
     - type: map_with_referer
       from:
         url: client-URL
       to:
         url: origin-server-URL
       redirect:
         url: redirect-URL
         regex:
           - regex1
           - regex2

At least one regular expression must be specified. In order to enable the 'deep linking
protection' feature in Traffic Server, configure records.yaml with:

.. code-block:: yaml
   :linenos:
   :emphasize-lines: 3

   records:
     http:
       referer_filter: 1

In order to enable run-time formatting for redirect URL, configure:

.. code-block:: yaml
   :linenos:
   :emphasize-lines: 3

   records:
     http:
       referer_format_redirect: 1

When run-time formatting for redirect-URL is enabled the following format
symbols can be used::

    %r - to substitute original "Referer" header string
    %f - to substitute client-URL from 'map_with_referer' record
    %t - to substitute origin-server-URL from 'map_with_referer' record
    %o - to substitute request URL to origin server, which was created as
         the result of a mapping operation

Note: There is a special referer type "~*" that can be used to specify that the Referer header is optional in the request. If "~*" referer
was used in map_with_referer mapping, only requests with Referer header will
be verified for validity.  If the "~" symbol was specified before a referer
regular expression, it means that the request with a matching referer header
will be redirected to redirectURL. It can be used to create a so-called
negative referer list.  If "*" was used as a referer regular expression -
all referrers are allowed.

Examples
--------

.. code-block:: yaml

   remap:
     - type: map_with_referer
       from:
         url: http://y.foo.bar.com/x/yy/
       to:
         url: http://foo.bar.com/x/yy/
       redirect:
         url: http://games.bar.com/new_games
         regex:
           - ".*\\.bar\\.com"
           - "www.bar-friends.com"

Explanation: Referer header must be in the request, only ".*\\.bar\\.com" and "www.bar-friends.com" are allowed.

.. code-block:: yaml

   remap:
     - type: map_with_referer
       from:
         url: http://y.foo.bar.com/x/yy/
       to:
         url: http://foo.bar.com/x/yy/
       redirect:
         url: http://games.bar.com/new_games
         regex:
           - "*"
           - "~.*\\.evil\\.com"

Explanation: Referer header must be in the request but all referrers are allowed except ".*\\.evil\\.com".

.. code-block:: yaml

   remap:
     - type: map_with_referer
       from:
         url: http://y.foo.bar.com/x/yy/
       to:
         url: http://foo.bar.com/x/yy/
       redirect:
         url: http://games.bar.com/error
         regex:
           - "~*"
           - "*"
           - "~.*\\.evil\\.com"

Explanation: Referer header is optional. However, if Referer header exists, only request from ".*\\.evil\\.com" will be redirected to redirect-URL.

Plugin Chaining
===============

Plugins can be configured to be evaluated in a specific order, passing
the results from one to the next (unless a plugin returns 0, then the
"chain" is broken).

Examples
--------

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://url/path
       to:
         url: http://url/path
       plugins:
         - name: /etc/traffic_server/config/plugins/plugin1.so
           params:
            - "1"
            - "2"
         - name: /etc/traffic_server/config/plugins/plugin2.so
           params:
            - "3"

This will pass "1" and "2" to plugin1.so and "3" to plugin2.so.

NextHop Selection Strategies
=============================

You may configure Nexthop or Parent hierarchical caching rules by remap using the
``strategy`` field.  See :doc:`../configuration/hierarchical-caching.en` and :doc:`strategies.yaml.en`
for configuration details and examples.

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: htpp://example.com
       to:
         url: http://backend.com
       strategy: my_strategy

ACL Filters
===========

In-line Filter
--------------

In-line filters can be created to control access of specific remap rules. The markup
is very similar to that of :file:`ip_allow.yaml`, structured as YAML mappings instead
of directive-based syntax.

Actions
~~~~~~~

Each ACL filter takes one of a number of actions specified by the ``action`` field:

- ``allow``: This behaves like the ``allow`` action in :file:`ip_allow.yaml` in which a list of allowed methods are
  provided. Any request with a method in the list is allowed, while any request with a method not in the list is denied.
  The exception to this is if :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to ``0``. In this case, the
  ``allow`` action is a synonym for ``add_allow``, described below.
- ``add_allow``: This action adds a list of allowed methods to whatever other methods are allowed in a subsequently
  matched ACL filter or :file:`ip_allow.yaml` rule.
- ``deny``: This behaves like the ``deny`` action in :file:`ip_allow.yaml` in which a list of denied methods are
  provided. Any request with a method in the list is denied, while any request with a method not in the list is allowed.
  The exception to this is if :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to ``0``. In this case, the
  ``deny`` action is a synonym for ``add_deny``, described below.
- ``add_deny``: This action adds a list of denied methods to whatever other methods are denied in a subsequently matched
  ACL filter or :file:`ip_allow.yaml` rule.

Filter Fields
~~~~~~~~~~~~~

-  ``src_ip`` -- source IP address or CIDR range (can be a list)
-  ``src_ip_invert`` -- inverted source IP address or CIDR range (can be a list)
-  ``src_ip_category`` -- source IP category name (string)
-  ``src_ip_category_invert`` -- inverted source IP category name (string)
-  ``in_ip`` -- incoming IP address or CIDR range (can be a list)
-  ``in_ip_invert`` -- inverted incoming IP address or CIDR range (can be a list)
-  ``method`` -- HTTP method or list of methods
-  ``action`` -- action to take (allow, deny, add_allow, add_deny)
-  ``internal`` -- boolean, matches internal requests only

Examples
~~~~~~~~

.. code-block:: yaml

   remap:
     - type: map
       from:
         url: http://foo.example.com/neverpost
       to:
         url: http://foo.example.com/neverpost
       acl_filter:
         action: deny
         method: post

     - type: map
       from:
         url: http://foo.example.com/onlypost
       to:
         url: http://foo.example.com/onlypost
       acl_filter:
         action: allow
         method: post

     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://foo.example.com/
       acl_filter:
         action: deny
         src_ip: 1.2.3.4

     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://foo.example.com/
       acl_filter:
         action: allow
         src_ip:
          - 10.5.2.1
         in_ip:
          - 72.209.23.4

     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://foo.example.com/
       acl_filter:
         action: allow
         src_ip: 127.0.0.1
         method:
          - post
          - get
          - head

     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://foo.example.com/
       acl_filter:
         action: allow
         src_ip_category: ACME_INTERNAL
         method:
          - post
          - get
          - head

Note that these ACL filters will return a 403 response if the resource is restricted.

The difference between ``src_ip`` and ``in_ip`` is that ``src_ip`` is the client
IP and ``in_ip`` is the IP address the client is connecting to (the incoming address).
``src_ip_category`` functions like ``ip_category`` described in :file:`ip_allow.yaml`.
If no IP address is specified for ``src_ip``, ``src_ip_category``, or
``in_ip``, the filter will implicitly apply to all incoming IP addresses. This
can be explicitly stated with ``src_ip: all``.

Named Filters
-------------

Named filters can be defined globally in the ``acl_filters`` section and then activated
or deactivated for blocks of mappings using filter directives.

Filter Directives
~~~~~~~~~~~~~~~~~

Filter directives are special entries in the ``remap`` sequence:

-  ``activate_filter: <name>`` -- activates a named filter for subsequent rules
-  ``deactivate_filter: <name>`` -- deactivates a named filter
-  ``delete_filter: <name>`` -- removes a filter definition
-  ``define_filter`` -- defines a new named filter inline

The ``internal`` operator can be used to filter on whether a request
is generated by |TS| itself, usually by a plugin. This operator
is helpful for remapping internal requests without allowing access
to external users. By default both internal and external requests
are allowed.

Examples
~~~~~~~~

.. code-block:: yaml

   acl_filters:
     disable_delete_purge:
       action: deny
       method:
        - delete
        - purge
     local_only:
       action: allow
       src_ip:
        - 192.168.0.1-192.168.0.254
        - 10.0.0.1-10.0.0.254

   remap:
     - activate_filter: disable_delete_purge

     - type: map
       from:
         url: http://foo.example.com/
       to:
         url: http://bar.example.com/

     - activate_filter: local_only

     - type: map
       from:
         url: http://www.example.com/admin
       to:
         url: http://internal.example.com/admin

     - deactivate_filter: local_only

     - type: map
       from:
         url: http://www.example.com/
       to:
         url: http://internal.example.com/

     - type: map
       from:
         url: http://auth.example.com/
       to:
         url: http://auth.internal.example.com/
       acl_filter:
         action: allow
         internal: true

The filter ``disable_delete_purge`` will be applied to all of the
mapping rules after it is activated. (It is activated before any mappings
and is never deactivated.) The filter ``local_only`` will only be applied to
the ``www.example.com/admin`` mapping.

Special Filter and ip_allow Named Filter
----------------------------------------

If :file:`ip_allow.yaml` has a "deny all" filter, it is treated as a special filter that is applied before remapping for
optimization. To control this for specific remap rules, a named filter called ``ip_allow`` is pre-defined. This named filter is
activated implicitly by default. To stop applying the special rule, disable the ``ip_allow`` filter as shown below.

.. code-block:: yaml

   # ip_allow.yaml
   ip_allow:
      - apply: in
        ip_addrs: 198.51.100.0/24
        action: deny
        method: ALL

.. code-block:: yaml

   # remap.yaml
   remap:
     - deactivate_filter: ip_allow
     - type: map ...
     - type: map ...
     - activate_filter: ip_allow

Note this entirely disables :file:`ip_allow.yaml` checks for those remap rules.

Evaluation Order and Matching Policy
------------------------------------

|TS| evaluates multiple ACL filters in the following order:

1. Special "deny all" filter in :file:`ip_allow.yaml`
2. In-line Filter in :file:`remap.yaml`
3. Named Filters in :file:`remap.yaml`
4. Filters in :file:`ip_allow.yaml`

When a matching ACL filter is found, |TS| stops processing subsequent ACL filters.

Note that step 1 happens at the start of the connection before any transactions are processed, unlike the other rules
here.  This is an optimization: if literally all requests are denied for a source IP address via an
:file:`ip_allow.yaml` rule, then there is no need to process any content from that IP for the connection at all, so the
connection is simply denied at the start.

.. note::

   The ACL filter behavior in :file:`remap.yaml` is identical to that in :file:`remap.config`.
   See :file:`remap.config` for details on ACL Action Behavior Changes for 10.x, Legacy Policy,
   Modern Policy, and examples of ACL filter combinations.

Including Additional Remap Files
=================================

The ``include`` directive allows mapping rules to be spread across
multiple files. The argument to the ``include`` directive is a file path
or directory path. Unless the path is absolute, it is resolved relative to the
Traffic Server configuration directory.

The effect of the ``include`` directive is as if the contents of
the included file(s) is included in the parent and parsing restarted
at the point of inclusion. This means that any filters defined in the
included files are global in scope, and that additional ``include``
directives are allowed.

.. note::

  Included remap files are currently tracked by the configuration
  subsystem. Changes to included remap files will be noticed
  by online configuration changes applied by :option:`traffic_ctl config reload`.

Examples
--------

In a top-level :file:`remap.yaml` file:

.. code-block:: yaml

   remap:
     - include: filters.yaml
     - include: one.example.com.yaml
     - include: two.example.com.yaml
     - include: /path/to/remap_fragments/  # directory

The file ``filters.yaml`` contains:

.. code-block:: yaml

   acl_filters:
     deny_purge:
       action: deny
       method: purge
     allow_purge:
       action: allow
       method: purge

The file ``one.example.com.yaml`` contains:

.. code-block:: yaml

   remap:
     - activate_filter: deny_purge
     - type: map
       from:
         url: http://one.example.com
       to:
         url: http://origin-one.example.com
     - deactivate_filter: deny_purge

The file ``two.example.com.yaml`` contains:

.. code-block:: yaml

   remap:
     - activate_filter: allow_purge
     - type: map
       from:
         url: http://two.example.com
       to:
         url: http://origin-two.example.com
     - deactivate_filter: allow_purge

Migration from remap.config
============================

The :file:`remap.yaml` format provides equivalent functionality to :file:`remap.config`
with YAML structure. Here are some common patterns:

remap.config to remap.yaml
---------------------------

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - remap.config
     - remap.yaml
   * - ::

         map http://www.x.com/ http://server.com/
     - .. code-block:: yaml

         remap:
           - type: map
             from:
               url: http://www.x.com/
             to:
               url: http://server.com/

   * - ::

         redirect http://old.com/ http://new.com/
     - .. code-block:: yaml

         remap:
           - type: redirect
             from:
               url: http://old.com/
             to:
               url: http://new.com/

   * - ::

         map http://example.com/ http://backend.com/ \
           @plugin=plugin.so @pparam=arg1

     - .. code-block:: yaml

         remap:
           - type: map
             from:
               url: http://example.com/
             to:
               url: http://backend.com/
             plugins:
               - name: plugin.so
                 params:
                  - arg1

   * - ::

         .definefilter my_filter @action=allow @src_ip=10.0.0.0/8
         .activatefilter my_filter
         map http://example.com/ http://backend.com/

     - .. code-block:: yaml

         acl_filters:
           my_filter:
             action: allow
             src_ip: 10.0.0.0/8

         remap:
           - activate_filter: my_filter
           - type: map
             from:
               url: http://example.com/
             to:
               url: http://backend.com/
