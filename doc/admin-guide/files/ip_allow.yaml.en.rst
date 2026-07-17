.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements. See the NOTICE file distributed with this work for additional information regarding
   copyright ownership. The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License. You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

.. include:: ../../common.defs
.. highlight:: yaml

.. _ip-allow:

===============
ip_allow.yaml
===============

.. configfile:: ip_allow.yaml

The :file:`ip_allow.yaml` file controls client access to |TS| and |TS| connections to upstream servers.
This control is specified via rules. Each rule has:

*  A direction (inbound or out).
*  A range of IP address to which the rule applies.
*  An action, either accept or deny.
*  A list of HTTP methods.

Inbound rules control access to |TS| from user agents. Outbound rules control access to upstream destinations
from |TS|. The IP addresses always apply to the remote address for |TS|. That is, the user agent IP address
for inbound rules and the upstream destination address for outbound rules. The rule can apply at the connection
level or just to specific methods.

|TS| can be updated for changes to the rules in :file:`ip_allow.yaml` file, by running the
:option:`traffic_ctl config reload`.

Format
======

:file:`ip_allow.yaml` is YAML format. The default configuration is::

   # YAML
   ip_allow:
     - apply: in
       ip_addrs: 127.0.0.1
       action: allow
       methods: ALL
     - apply: in
       ip_addrs: ::1
       action: allow
       methods: ALL
     - apply: in
       ip_addrs: 0/0
       action: deny
       methods:
         - PURGE
         - PUSH
         - DELETE
         - TRACE
     - apply: in
       ip_addrs: ::/0
       action: deny
       methods:
         - PURGE
         - PUSH
         - DELETE
         - TRACE
     - apply: out
       ip_addrs:
         - 0.0.0.0/8
         - 127.0.0.0/8
         - "::"
         - ::1
         - 10.0.0.0/8
         - 172.16.0.0/12
         - 192.168.0.0/16
         - 169.254.0.0/16
         - ::/96
         - fc00::/7
         - fe80::/10
         - ::ffff:0:0/96
       action: deny
       methods: CONNECT

.. important::

   Upgrade note: newly generated default :file:`ip_allow.yaml` files deny outbound
   ``CONNECT`` tunnels to unspecified, loopback, private, link-local,
   IPv4-compatible IPv6, and IPv4-mapped IPv6 destination ranges. Existing custom files are not
   rewritten, but forward proxy ``CONNECT`` requests and SNI ``tunnel_route`` targets
   in these ranges require an explicit ``apply: out`` allow rule before the default
   deny rule. Outbound :file:`ip_allow.yaml` policy also applies to ``forward_route``
   and ``partial_blind_route`` upstream destinations, so configure explicit policy for
   intentional local or private routes.

Each rule is a mapping. The YAML data must have a top level key of "ip_allow" and its value must
be a mapping or a sequence of mappings, each of those being one rule.

The keys in a rule are:

``apply``
   This is where the rule is applied, either ``in`` or ``out``. Inbound application means
   the rule is applied to inbound user agent connections. Outbound application means the rule is
   applied to outbound connections from |TS| to an upstream destination. This is a required key.

``ip_addrs``
   IP addresses to match for the rule to be applied. This can be either an address range or an
   array of address ranges. This is a required key.

``action``
   The action, which must be ``allow`` or ``deny``. This is a required key.

``methods``
   This is optional. If not present, the rule action applies to all methods. If present, the rule
   action is applied to connections using those methods and its opposite to all other connections. The
   keyword "ALL" means all methods, making the specification of any other method redundant. All
   methods comparisons are case insensitive. This is an optional key.

An IP address range can be specified in several ways. A range is always IPv4 or IPv6, it is not
allowed to have a range that contains addresses from different IP address families.

*  A single address, which specifies a range of size 1, e.g. "127.0.0.1".
*  A minimum and maximum address separated by a dash, e.g. "10.1.0.0-10.1.255.255".
*  A CIDR based value, e.g. "10.1.0.0/16", which is a range containing exactly the specified network.

A rule must have the ``apply``, ``ip_addrs``, and ``action`` keys. Rules match based on
IP addresses only and are then applied to all matching sessions. If the rule is an ``allow`` rule,
the specified methods are allowed and all other methods are denied. If the rule is a ``deny`` rule,
the specified methods are denied and all other methods are allowed.

For example, from the default configuration, the rule for ``127.0.0.1`` is ``allow`` with all
methods. Therefore an inbound connection from the loopback address (127.0.0.1) is allowed to use any
method. The general IPv4 rule, covering all IPv4 address, is a ``deny`` rule and therefore when it
matches the methods "PURGE", "PUSH", "DELETE", and "TRACE", these methods are denied and any other method
is allowed.

The rules are matched in order, by IP address, therefore the general IPv4 rule does not apply to the
loopback address because the latter is matched first.

A major difference in application between ``in`` and ``out`` rules is that by default,
inbound connections are denied and therefore if there is no rule that matches, the connection is
denied. Outbound rules allow by default if no rule matches. The default configuration includes
explicit outbound rules that deny ``CONNECT`` tunnels to unspecified, loopback, private,
link-local, and IPv4-mapped IPv6 destination addresses.

.. note::

   A ``tunnel_route`` in :file:`sni.yaml` synthesizes a ``CONNECT`` transaction, so outbound
   ``ip_allow`` rules are applied to its destination address before |TS| opens the upstream
   connection. ``forward_route`` and ``partial_blind_route`` upstream destinations are also
   subject to outbound ``ip_allow`` policy. The data flowing through these routes is still not
   interpreted as HTTP by |TS|. For details, see :ref:`sni-routing` and :file:`sni.yaml`.

Examples
========

The following example enables all clients access.::

   apply: in
   ip_addrs: 0.0.0.0-255.255.255.255
   action: allow

The following example allows access to all clients on addresses in a subnet::

   apply: in
   ip_addrs: 123.12.3.000-123.12.3.123
   action: allow

The following example denies access all clients on addresses in a subnet::

   apply: in
   ip_addrs: 123.45.6.0-123.45.6.123
   action: deny

If the entire subnet were to be denied, that would be::

   apply: in
   ip_addrs: 123.45.6.0/24
   action: deny

The following example allows any method to any upstream servers::

   apply: out
   ip_addrs: 0.0.0.0-255.255.255.255
   action: allow

Alternatively this can be done with::

   apply: out
   ip_addrs: 0/0
   action: allow

Or also by having no rules at all, as outbound by default is allow.

The following example denies to access all servers on a specific subnet::

   apply: out
   ip_addr: 10.0.0.0-10.0.255.255
   action: deny

Alternatively::

   apply: out
   ip_addrs: 10.0.0.0/16
   action: deny

The ``ip_addrs`` can be an array of ranges, so that::

   - apply: in
     ip_addrs: 10.0.0.0/8
     action: deny
   - apply: in
     ip_addrs: 172.16.0.0/20
     action: deny
   - apply: in
     ip_addrs: 192.168.1.0/24
     action: deny

can be done more simply as::

  apply: in
  ip_addrs:
    - 10.0.0.0/8
    - 172.16.0.0/20
    - 192.168.1.0/24
  action: deny

If the goal is to allow only ``GET`` and ``HEAD`` requests to those servers, it would be::

   apply: out
   ip_addrs: 10.0.0.0/16
   methods: [ GET, HEAD ]
   action: allow

Alternatively::

   apply: out
   ip_addrs: 10.0.0.0/16
   methods:
   - GET
   - HEAD
   action: allow

This will match the IP address for the target servers on the outbound connection. Then, if the
method is ``GET`` or ``HEAD`` the connection will be allowed, otherwise the connection will be
denied.

The default configuration denies ``CONNECT`` tunnels to unspecified, loopback, private,
link-local, and IPv4-mapped IPv6 destinations. To intentionally allow a private tunnel
destination, add the more specific outbound rule before the default deny rule::

   - apply: out
     ip_addrs: 127.0.0.1
     action: allow
     methods: CONNECT

Because the first matching IP range selects the rule, an allow rule with a method list denies other
methods to the same destination. If the same destination also receives non-``CONNECT`` traffic, add
those methods to the exception or use ``methods: ALL``.

As a final example, here is the default configuration in compact form::

   ip_allow: [
     { apply: in, ip_addrs: 127.0.0.1, action: allow },
     { apply: in, ip_addrs: "::1", action: allow },
     { apply: in, ip_addrs: 0/0, action: deny, methods: [ PURGE, PUSH, DELETE, TRACE ] },
     { apply: in, ip_addrs: "::/0", action: deny, methods: [ PURGE, PUSH, DELETE, TRACE ] },
     { apply: out,
       ip_addrs: [ 0.0.0.0/8, 127.0.0.0/8, "::", "::1", 10.0.0.0/8, 172.16.0.0/12,
                   192.168.0.0/16, 169.254.0.0/16, "::/96", "fc00::/7",
                   "fe80::/10", "::ffff:0:0/96" ],
       action: deny,
       methods: [ CONNECT ] }
     ]

.. note::

   For ATS 9.0, this file is (almost) backwards compatible. If the first line is a single '#'
   character, or contains only "# ats", then the file will be read in the version 8.0 format. This
   is true for the default format and so if that has not been changed it should still work. This
   allows a grace period before ATS 10.0 which will drop the old format entirely.
