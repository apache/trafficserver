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

============
remap.config
============

.. configfile:: remap.config

.. include:: ../../common.defs

.. toctree::
   :maxdepth: 2


The :file:`remap.config` file (by default, located in
``/usr/local/etc/trafficserver/``) contains mapping rules that Traffic Server
uses to perform the following actions:

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

After you modify the :file:`remap.config` run the
:option:`traffic_ctl config reload` to apply the changes. The current configuration is replaced
with the new configuration only if there are no errors in the file. Any syntax error will prevent
an update. Even if syntactically correct the file is considered valid only if it has at least :ts:cv:`proxy.config.url_remap.min_rules_required`
rules in it. This defaults to 0, but can be set higher if it is desirable to prevent loading an
empty or missing file.

Format
======

Each line in the :file:`remap.config` file must contain a mapping rule. Empty lines,
or lines starting with ``#`` are ignored. Each line can be broken up into multiple
lines for better readability by using ``\`` as continuation marker.

Traffic Server recognizes three space-delimited fields: ``type``,
``target``, and ``replacement``. The following list describes the format of each field.

.. _remap-config-format-type:

``type``
    Enter one of the following:

    -  ``map`` --translates an incoming request URL to the appropriate
       origin server URL.

    -  ``map_with_recv_port`` --exactly like 'map' except that it uses the port at
       which the request was received to perform the mapping instead of the port present
       in the request. The regex qualifier can also be used for this type. When present,
       'map_with_recv_port' mappings are checked first. If there is a match, then it is
       chosen without evaluating the "regular" forward mapping rules.

    -  ``map_with_referer`` -- extended version of 'map', which can be used to activate
       "deep linking protection", where target URLs are only accessible when the Referer
       header is set to a URL that is allowed to link to the target.

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

       .. note:: use the ``regex_`` prefix to indicate that the line has a regular expression (regex).

.. _remap-config-format-target:

``target``
    Enter the request ("from") URL. You can enter up to four components: ::

        scheme://host:port/path_prefix

    where ``scheme`` is ``http``, ``https``, ``ws`` or ``wss``.

.. _remap-config-format-replacement:

``replacement``
    Enter the origin ("to") URL. You can enter up to four components: ::

        scheme://host:port/path_prefix

    where ``scheme`` is ``http``, ``https``, ``ws`` or ``wss``.

   .. note:: A remap rule for requests that upgrade from HTTP to WebSocket still require a remap rule with the ``ws`` or ``wss`` scheme.


.. _remap-config-precedence:

Precedence
==========

Remap rules are not processed top-down, but based on an internal
priority. Once these rules are executed we pick the first match
based on configuration file parse order.

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

A map rule with a single ``/`` acts as a wildcard, it will match any
request. This should be use with care, and certainly only once at the
end of the remap.config file. E.g.

::

    map / http://all.example.com

Examples
--------

The following section shows example mapping rules in the
:file:`remap.config` file.

Reverse Proxy Mapping Rules
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example shows a map rule that does not specify a path
prefix in the target or replacement: ::

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
the target: ::

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
``http://www.y.com/engineering/marketing/requirements.html``    ``http://engineering.y.com/marketing/requirements.html``
=============================================================== ==========================================================

The following example shows that the order of the rules matters: ::

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

This is different if one rule does not have a host. For example consider these rules using the `Match-All`_ rule::

   map / http://127.0.0.1:8001/
   map http://example.com/dist_get_user http://127.0.0.1:8001/denied.html

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
different phases making the order irrelevant. ::

   map http://example.com/dist_get_user http://127.0.0.1:8001/denied.html
   map / http://127.0.0.1:8001/

The following example shows a mapping with a path prefix specified in
the target and replacement::

   map http://www.h.com/a/b/ http://server.h.com/customers/x/y
   reverse_map http://server.h.com/customers/x/y/ http://www.h.com/a/b/

This rule results in the following translation.

===================================== ==================================================
Client Request                        Translated Request
===================================== ==================================================
``http://www.h.com/a/b/c/d/doc.html`` ``http://server.h.com/customers/x/y/c/d/doc.html``
``http://www.h.com/a/index.html``     ``Translation fails``
===================================== ==================================================

The following example shows reverse-map rules::

    map http://www.x.com/ http://server.hoster.com/x/
    reverse_map http://server.hoster.com/x/ http://www.x.com/

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
~~~~~~~~~~~~~~~~~~~~~~

The following rule permanently redirects all HTTP requests for
``www.company.com`` to ``www.company2.com``: ::

    redirect http://www.company.com/ http://www.company2.com/

The following rule *temporarily* redirects all HTTP requests for
``www.company1.com`` to ``www.company2.com``: ::

    redirect_temporary http://www.company1.com/ http://www.company2.com/

.. _remap-config-regex:

Regular Expression (regex) Remap Support
========================================

Regular expressions can be specified in remapping rules, with the
limitations below:

-  Only the ``host`` field can contain a regex; the ``scheme``,
   ``port``, and other fields cannot. For path manipulation via regexes,
   use the :ref:`admin-plugins-regex-remap`.
-  The number of capturing subpatterns is limited to 9. This means that
   ``$0`` through ``$9`` can be used as subtraction placeholders (``$0``
   will be the entire input string).
-  The number of substitutions in the expansion string is limited to 10.
-  There is no ``regex_`` equivalent to ``reverse_remap``, so when using
   ``regex_map`` you should make sure the reverse path is clear by
   setting (:ts:cv:`proxy.config.url_remap.pristine_host_hdr`)

Examples
--------

::

    regex_map http://x([0-9]+).z.com/ http://real-x$1.z.com/
    regex_redirect http://old.(.*).z.com http://new.$1.z.com

.. _map_with_referer:

map_with_referer
================

the format of is the following::

    map_with_referer client-URL origin-server-URL redirect-URL regex1 [regex2 ...]

'redirect-URL' is a redirection URL specified according to RFC 2616 and can
contain special formatting instructions for run-time modifications of the
resulting redirection URL.  All regexes Perl compatible  regular expressions,
which describes the content of the "Referer" header which must be
verified. In case an actual request does not have "Referer" header or it
does not match with referer regular expression, the HTTP request will be
redirected to 'redirect-URL'.

At least one regular expressions must be specified in order to activate
'deep linking protection'.  There are limitations for the number of referer
regular expression strings - 2048.  In order to enable the 'deep linking
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

When run-time formatting for redirect-URL was enabled the following format
symbols can be used::

    %r - to substitute original "Referer" header string
    %f - to substitute client-URL from 'map_with_referer' record
    %t - to substitute origin-server-URL from 'map_with_referer' record
    %o - to substitute request URL to origin server, which was created a
         the result of a mapping operation

Note: There is a special referer type "~*" that can be used in order to
specify that the Referer header is optional in the request.  If "~*" referer
was used in map_with_referer mapping, only requests with Referer header will
be verified for validity.  If the "~" symbol was specified before referer
regular expression, it means that the request with a matching referer header
will be redirected to redirectURL. It can be used to create a so-called
negative referer list.  If "*" was used as a referer regular expression -
all referrers are allowed.  Various combinations of "*" and "~" in a referer
list can be used to create different filtering rules.

map_with_referer Examples
-------------------------

::

   map_with_referer http://y.foo.bar.com/x/yy/  http://foo.bar.com/x/yy/ http://games.bar.com/new_games .*\.bar\.com www.bar-friends.com

Explanation: Referer header must be in the request, only ".*\.bar\.com" and "www.bar-friends.com" are allowed.

::

   map_with_referer http://y.foo.bar.com/x/yy/  http://foo.bar.com/x/yy/ http://games.bar.com/new_games * ~.*\.evil\.com

Explanation: Referer header must be in the request but all referrers are allowed except ".*\.evil\.com".

::

    map_with_referer http://y.foo.bar.com/x/yy/  http://foo.bar.com/x/yy/ http://games.bar.com/error ~* * ~.*\.evil\.com

Explanation: Referer header is optional. However, if Referer header exists, only request from ".*\.evil\.com" will be redirected to redirect-URL.


.. _remap-config-plugin-chaining:

Plugin Chaining
===============

Plugins can be configured to be evaluated in a specific order, passing
the results from one in to the next (unless a plugin returns 0, then the
"chain" is broken).

Examples
--------

::

    map http://url/path http://url/path \
        @plugin=/etc/traffic_server/config/plugins/plugin1.so @pparam=1 @pparam=2 \
        @plugin=/etc/traffic_server/config/plugins/plugin2.so @pparam=3

will pass "1" and "2" to plugin1.so and "3" to plugin2.so.

This will pass "1" and "2" to plugin1.so and "3" to plugin2.so

.. _remap-config-named-filters:

NextHop Selection Strategies
============================

You may configure Nexthop or Parent hierarchical caching rules by remap using the
**@strategy** tag.  See :doc:`../configuration/hierarchical-caching.en` and :doc:`strategies.yaml.en`
for configuration details and examples.

.. _acl-filters:

ACL Filters
===========

In-line Filter
--------------

In-line filters can be created to control access of specific remap lines. The markup
is very similar to that of :file:`ip_allow.yaml`, with slight changes to
accommodate remap markup.

Actions
~~~~~~~

As is the case with :file:`ip_allow.yaml` rules, each ACL filter takes one of a number of actions. They are specified as
``@action=<action>``, such as ``@action=add_allow``. There are four possible actions:

- ``allow``: This behaves like the ``allow`` action in :file:`ip_allow.yaml` in which a list of allowed methods are
  provided. Any request with a method in the list is allowed, while any request with a method not in the list is denied.
  The exception to this is if :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to ``0``. In this case, the
  ``allow`` action is a synonym for ``add_allow``, described below.
- ``add_allow``: This action adds a list of allowed methods to whatever other methods are allowed in a subsequently
  matched ACL filter or :file:`ip_allow.yaml` rule. Thus, if an ``add_allow`` ACL filter specifies the ``POST`` method,
  and a subsequently matching :file:`ip_allow.yaml` rule allows the ``GET`` and ``HEAD`` methods, then any requests that
  have ``POST``, ``GET``, or ``HEAD`` methods will be allowed while all others will be denied.
- ``deny``: This behaves like the ``deny`` action in :file:`ip_allow.yaml` in which a list of denied methods are
  provided. Any request with a method in the list is denied, while any request with a method not in the list is allowed.
  The exception to this is if :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to ``0``. In this case, the
  ``deny`` action is a synonym for ``add_deny``, described below.
- ``add_deny``: This action adds a list of denied methods to whatever other methods are denied in a subsequently matched
  ACL filter or :file:`ip_allow.yaml` rule. Thus, if an ``add_deny`` ACL filter specifies the ``POST`` method, and a
  matching :file:`ip_allow.yaml` rule allows the ``GET``, ``HEAD``, and ``POST`` methods, then this ACL filter
  effectively removes ``POST`` from the allowed method list. Thus only requests with the ``GET`` and ``HEAD`` methods
  will be allowed.

Examples
~~~~~~~~

::

    map http://foo.example.com/neverpost  http://foo.example.com/neverpost @action=deny @method=post
    map http://foo.example.com/onlypost  http://foo.example.com/onlypost @action=allow @method=post

    map http://foo.example.com/  http://foo.example.com/ @action=deny @src_ip=1.2.3.4
    map http://foo.example.com/  http://foo.example.com/ @action=allow @src_ip=127.0.0.1

    map http://foo.example.com/  http://foo.example.com/ @action=allow @src_ip=10.5.2.1 @in_ip=72.209.23.4

    map http://foo.example.com/  http://foo.example.com/ @action=allow @src_ip=127.0.0.1 @method=post @method=get @method=head

    map http://foo.example.com/  http://foo.example.com/ @action=allow @src_ip_category=ACME_INTERNAL @method=post @method=get @method=head

Note that these ACL filters will return a 403 response if the resource is restricted.

The difference between ``@src_ip`` and ``@in_ip`` is that the ``@src_ip`` is the client
ip and the ``in_ip`` is the ip address the client is connecting to (the incoming address).
``@src_ip_category`` functions like ``ip_category`` described in :file:`ip_allow.yaml`.
If no IP address is specified for ``@src_ip``, ``@src_ip_category``, or
``@in_ip``, the filter will implicitly apply to all incoming IP addresses. This
can be explicitly stated with ``@src_ip=all``.

Named Filters
-------------

Named filters can be created and applied to blocks of mappings using
the ``.definefilter``, ``.activatefilter``, and ``.deactivatefilter``
directives. Named filters must be defined using ``.definefilter`` before
being used. Once defined, ``.activatefilter`` can used to activate a
filter for all mappings that follow until deactivated with
``.deactivatefilter``.

The ``@internal`` operator can be used to filter on whether a request
is generated by |TS| itself, usually by a plugin. This operator
is helpful for remapping internal requests without allowing access
to external users. By default both internal and external requests
are allowed.

Examples
~~~~~~~~

::

    .definefilter disable_delete_purge @action=deny @method=delete @method=purge
    .definefilter local_only @action=allow @src_ip=192.168.0.1-192.168.0.254 @src_ip=10.0.0.1-10.0.0.254

    .activatefilter disable_delete_purge

    map http://foo.example.com/ http://bar.example.com/

    .activatefilter local_only
    map http://www.example.com/admin http://internal.example.com/admin
    .deactivatefilter local_only

    map http://www.example.com/ http://internal.example.com/
    map http://auth.example.com/ http://auth.internal.example.com/ @action=allow @internal

The filter `disable_delete_purge` will be applied to all of the
mapping rules. (It is activated before any mappings and is never
deactivated.) The filter `local_only` will only be applied to the
second mapping.

Special Filter and ip_allow Named Filter
----------------------------------------

If :file:`ip_allow.yaml` has a "deny all" filter, it is treated as a special filter that is applied before remapping for
optimizaion. To control this for specific remap rules, a named filter called ``ip_allow`` is pre-defined. This named filter is
activated implicitly in default. To stop applying the special rule, disable the ``ip_allow`` filter as shown below.

::

   # ip_allow.yaml
   ip_allow:
      - apply: in
        ip_addrs: 198.51.100.0/24
        action: deny
        method: ALL

   # remap.config
   .deactivatefilter ip_allow
   map ...
   map ...
   .activatefilter ip_allow

Note this entirely disables :file:`ip_allow.yaml` checks for those remap rules.

Evaluation Order and Matching Policy
------------------------------------

ATS evaluates multiple ACL filters in the following order:

1. Special "deny all" filter in :file:`ip_allow.yaml`
2. In-line Filter in :file:`remap.config`
3. Named Filters in :file:`remap.config`
4. Filters in :file:`ip_allow.yaml`

When an ACL filter is found, ATS stops processing subsequent ACL filters depending on the policy configured by
:ts:cv:`proxy.config.url_remap.acl_behavior_policy`.

Note the step 1 happens at the start of the connection before any transactions are processed, unlike the other rules here.

.. note::

   ATS v10 introduced following matching policies. Prior to the change, ATS traverses all matched ACL filters by IP and "deny"
   action had priority.

ACL Action Behavior Changes for 10.x
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Before |TS| 10.x, ACL filters used the same action names as :file:`ip_allow.yaml` (``allow`` and ``deny``) but these
actions behaved differently.

- As described in :file:`ip_allow.yaml`, for any transaction matching an :file:`ip_allow.yaml` rule whose action is
  ``allow``, any request with a method in the allow list will be accepted, while all other methods are denied. ``deny``
  actions instead list methods which are denied, while all other methods are implicitly allowed.
- These same action names for ACL filters, on the other hand, functioned essentially additively in nature. For instance,
  an ``allow`` ACL filter action would list a set of methods which would be allowed in addition to any other allowed
  methods specified by :file:`ip_allow.yaml` rules.  Similarly, a ``deny`` ACL filter action would list a set of methods
  which would be denied in addition to any other denied methods specified by :file:`ip_allow.yaml` rules.

This difference in behavior lead at times to confusion as users would expect the two actions having the same name to
behave the same across the ACL filter and :file:`ip_allow.yaml` rule systems.

For |TS| 10.x, these ACL filter actions have been changed to be more consistent with :file:`ip_allow.yaml` rules while
still allowing additive action behavior via two new rules: ``add_allow`` and ``add_deny``:

- ``allow``: as with ``allow`` actions for :file:`ip_allow.yaml` rules, this action specifies a list of HTTP methods
  which are allowed. All requests with a methods in this list will be allowed, requests with methods not in this
  list will be denied, and no other filters or :file:`ip_allow.yaml` rules will be applied.
- ``deny``: as with ``deny`` actions for :file:`ip_allow.yaml` rules, this action now specifies a list of HTTP methods
  which are denied. All requests with a method in this list will be denied, all other requests will be allowed, and
  no other filters or :file:`ip_allow.yaml` rules will be applied.
- ``add_allow``: this action, new to |TS| 10.x, behaves like ``allow`` used to behave for ACL filters pre |TS| 10.x:
  that is, it adds methods which are allowed to the :file:`ip_allow.yaml` rule otherwise matched for the given
  transaction.
- ``add_deny``: this action, new to |TS| 10.x, behaves like ``deny`` used to behave for ACL filters pre |TS| 10.x: that
  is, it adds methods which are denied the :file:`ip_allow.yaml` rule otherwise matched for the given transaction.

This change to ``allow`` and ``deny`` ACL filter actions is backwards incompatible with pre |TS| 10.x releases. In order to avoid
surprises when transitioning to |TS| 10.x, by default, the ``allow`` and ``deny`` ACL filter actions rather behave like
they did in |TS| 9.x: that is, they behave the same as ``add_allow`` and ``add_deny``. This behavior is deprecated in
|TS| 10.x with the ``allow`` and ``deny`` ACL filter actions acting like they do for :file:`ip_allow.yaml` being the
default behavior for the future |TS| 11.x release.

Legacy Policy
~~~~~~~~~~~~~

This is the default ACL behavioral policy and it is configured by setting
:ts:cv:`proxy.config.url_remap.acl_behavior_policy` to ``0``. With this policy, ACL filter ``allow`` and ``deny``
actions behave like they did pre |TS| 10.x: they additively add allowed or denied methods for transactions.
Functionally, this means that with this legacy policy configured, ``allow`` is a synonym for ``add_allow`` and ``deny``
is a synonym for ``add_deny``.  Because of this, an organization can easily, and incrementally prepare for the modern
ACL filter action behavior by transitioning a rule one at a time by simply replacing ``allow`` with ``add_allow`` and
``deny`` with ``add_deny``. Once all actions are transitioned, an organization can then switch to the modern policy.

Consider a filter like the following:

::

   map http://www.example.com/ http://internal.example.com/ @action=deny @method=POST

The implicit ``@src_ip`` is all client IP addresses, so this filter will match on any ``POST`` request matched by this remap rule
from any client and its action will be to deny such POST requests. For all other methods, the filter will not take effect, thus
allowing other active ACL filters or an :file:`ip_allow.yaml` rule to determine the action to take for any other transaction.

With the legacy policy being described here, this filter acts identically with an ``add_deny`` action. Thus, the above
rule is identical with:

::

   map http://www.example.com/ http://internal.example.com/ @action=add_deny @method=POST

.. note::

   This policy's behavior is similar to ATS v9 and older, but employs "first match wins" policy.

Modern Policy
~~~~~~~~~~~~~

As described above, with this policy, ACL filter ``allow`` and ``deny`` actions function as they do for
:file:`ip_allow.yaml` rules.  Filters that instead want to additively allow or deny methods to base
:file:`ip_allow.yaml` rules should use the ``add_allow`` and ``add_deny`` actions.  Modern ACL action policy is
configured by setting :ts:cv:`proxy.config.url_remap.acl_behavior_policy` to ``1``.

Consider a filter like the following (the same as above):

::

   map http://www.example.com/ http://internal.example.com/ @action=deny @method=POST

The implicit ``@src_ip`` is all client IP address, so this filter will apply to **all** requests matching this remap rule. Again,
like an analogously crafted :file:`ip_allow.yaml` action rule, this will deny ``POST`` requests while allowing **all** other methods
to the ``www.example.com``. No other ACL filters or :file:`ip_allow.yaml` rules will be applied for any request to this target.

On the other hand, if one wants to instead additively deny ``POST`` requests like they can in legacy mode, they would
simply use the ``add_deny`` action:

::

   map http://www.example.com/ http://internal.example.com/ @action=add_deny @method=POST

This rule behaves identically with the corresponding ``@action=deny`` rule described above in the Legacy Mode section:
it will ensure that ``POST`` requests are denied while leaving all other methods to other :file:`ip_allow.yaml` rules or
ACL filters to determine how they should be handled.

Considering again the ``@action=deny`` example, it is naturally not very likely that someone will simply want to deny
just ``POST`` methods while allowing all other methods. A more realistic ``@action=deny`` example is the following:

::

   map http://www.example.com/ http://internal.example.com/ @action=allow @method=GET @method=HEAD

The implicit ``@src_ip`` is all client IP address, so this filter will apply to all transactions matching this remap
rule. Again, like an analogously crafted :file:`ip_allow.yaml` rule, this will allow ``GET`` and ``HEAD`` requests
while denying all other methods to the ``internal.example.com`` origin. No other ACL filters or ip_allow rules will
apply for this target. Note that this demonstrates that the modern policy adds the ability to specify actions like
:file:`ip_allow.yaml` rules via ``allow` and ``deny`` actions while also preserving the |TS| 9.x ability to additively
specify allowed or denied methods via ``add_allow`` and ``add_deny`` actions. Thus, the modern policy is a feature
superset compared to the legacy policy.

.. warning::

   As mentioned above, carefully consider that with the "Modern" ACL filter behavior, ``@action=deny`` and
   ``@action=allow`` actions behave differently than they did in |TS| 9.x and older. Thus ACL filters will almost
   certainly need to be updated when transitioning to the modern policy. As a start, one may simply want to replace
   ``@action=allow`` with ``@action=add_allow`` and ``@action=deny`` with ``@action=add_deny`` to bring the rules to
   parity with pre |TS| 10.x behavior. In any case, the default policy is the legacy policy to ease this transition
   before this new behavior becomes the default in |TS| 11.x.

Upgrade Notes
~~~~~~~~~~~~~

As explained above, for 10.x, by default :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to ``0`` to maintain
ACL filter action backwards compatibility with |TS| 9.x and older :file:`remap.config` files. This means that ACL filter
actions ``allow`` and ``deny`` behave additively like ``add_allow`` and ``add_deny`` just like they did for 9.x and
older. This is to ease the transition to the modern ACL filter behavior.

When an organization is ready to upgrade to the new ACL filter action behavior, they need to make sure that their
:file:`remap.config` ACL filter actions are configured correctly for the expected action behavior. To maintain 9.x
behavior, this simply means replacing ``allow`` with ``add_allow`` and ``deny`` with ``add_deny``. In case it is
helpful, ``tools/remap/convert_remap_actions_to_10x`` is provided to do this replacement programmatically.  Once all
rules are updated, the organization can then switch to the modern policy by setting
:ts:cv:`proxy.config.url_remap.acl_behavior_policy` to ``1``.  :ts:cv:`proxy.config.url_remap.acl_behavior_policy` will
be removed in 11.x and the modern policy will be the default and only behavior. As explained above, once an organization
has transitioned to the new behavior, they are then able to use the new ``allow`` and ``deny`` action functionality that
behaves like :file:`ip_allow.yaml` rules (see "Modern Policy" above for details).


Example of ACL filter combinations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As described above, the new ``add_allow`` and ``add_deny`` actions behave the same in both legacy and modern ACL filter policies: they add
to underlying :file:`ip_allow.yaml` rules methods that are either allowed or denied, respectively. The difference in behavior with these
policies is how ``allow`` and ``deny`` actions behave. To help ensure that the behaviors between these two modes is understood, this section
walks through examples of differences between these two policies for ``allow`` and ``deny`` actions.

This is an example of in-line filter, named filters in :file:`remap.config`, and :file:`ip_allow.yaml`.

::

   # ip_allow.yaml
   ip_allow:
      - apply: in
        ip_addrs: [0/0, ::/0]
        action: deny
        method: [PURGE, PUSH]

   # remap.config
   .definefilter named-filter-1 @action=allow @method=HEAD
   .definefilter named-filter-2 @action=deny @method=DELETE

   .activatefilter named-filter-1
   .activatefilter named-filter-2

   map http://www.example.com/ http://internal.example.com/ @action=deny @method=POST

With the "Legacy" policy, the evaluation applied from left to right until match is found:

====== ============== ============== ============== ================ =============
Method In-line Filter Named Filter 1 Named Filter 2 ip_allow.yaml    result
====== ============== ============== ============== ================ =============
GET    \-             \-             \-             allow (implicit) allowed (200)
POST   deny           \-             \-             \-               denied  (403)
HEAD   \-             allow          \-             \-               allowed (200)
DELETE \-             \-             deny           \-               denied  (403)
PURGE  \-             \-             \-             deny             denied  (403)
PUSH   \-             \-             \-             deny             denied  (403)
====== ============== ============== ============== ================ =============

With the "Modern" policy, the in-line filter works like an :file:`ip_allow.yaml` rulei: it applies to all requests to
``www.example.com`` and denies ``POST`` requests while allowing all other methods:

====== ================ ============== ============== ============= =============
Method In-line Filter   Named Filter 1 Named Filter 2 ip_allow.yaml result
====== ================ ============== ============== ============= =============
GET    allow            \-             \-             would allow   allowed (200)
POST   deny             \-             \-             \-            denied  (403)
HEAD   allow            would allow    \-             \-            allowed (200)
DELETE allow            \-             would deny     \-            allowed (200)
PURGE  allow            \-             \-             would deny    allowed (200)
PUSH   allow            \-             \-             would deny    allowed (200)
====== ================ ============== ============== ============= =============

Including Additional Remap Files
================================

The ``.include`` directive allows mapping rules to be spread across
multiple files. The argument to the ``.include`` directive is a
list of file names to be parsed for additional mapping rules. Unless
the names are absolute paths, they are resolved relative to the
Traffic Server configuration directory.

The effect of the ``.include`` directive is as if the contents of
the listed files is included in the parent and parsing restarted
at the point of inclusion. This means that and filters named in the
included files are global in scope, and that additional ``.include``
directives are allowed.

.. note::

  Included remap files are not currently tracked by the configuration
  subsystem. Changes to included remap files will not be noticed
  by online configuration changes applied by :option:`traffic_ctl config reload`
  unless :file:`remap.config` has also changed.

Examples
--------

In this example, a top-level :file:`remap.config` file simply
references additional mapping rules files ::

  .include filters.config
  .include one.example.com.config two.example.com.config

The file `filters.config` contains ::

  .definefilter deny_purge @action=deny @method=purge
  .definefilter allow_purge @action=allow @method=purge

The file `one.example.com.config` contains::

  .activatefilter deny_purge
  map http://one.example.com http://origin-one.example.com
  .deactivatefilter deny_purge

The file `two.example.com.config` contains::

  .activatefilter allow_purge
  map http://two.example.com http://origin-two.example.com
  .deactivatefilter allow_purge
