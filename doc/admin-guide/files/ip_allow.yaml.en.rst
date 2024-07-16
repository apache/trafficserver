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
*  A range of IP addresses or an IP category to which the rule applies.
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

Each rule is a mapping. The YAML data must have a top level key of "ip_allow" and its value must
be a mapping or a sequence of mappings, each of those being one rule.

The keys in a rule are:

``apply``
   This is where the rule is applied, either ``in`` or ``out``. Inbound application means
   the rule is applied to inbound user agent connections. Outbound application means the rule is
   applied to outbound connections from |TS| to an upstream destination. This is a required key.

``ip_addrs``
   IP addresses to match for the rule to be applied. This can be either an address range or an
   array of address ranges. Either this or ``ip_categories`` are required keys for a rule.

``ip_categories``
   A user defined string identifying a category of IP addresses relevant to a particular network.
   For example, ``ACME_INTERNAL`` might represent the set of IP addresses for hosts within a
   company's network. ``ACME_EXTERNAL`` might represet hosts belonging to the company's network, but
   which are outside the company's firewall. ``ACME_ALL`` could be used to represent the set of both
   of these categories. Multiple categories can be specified as an array of strings.

   The set of IP ranges belonging to each category is specified via the separate ``ip_categories``
   root level node. The :file:`ip_allow.yaml` parser also supports supplying the IP categories via
   an external file specified with the :ts:cv:`proxy.config.cache.ip_categories.filename`
   configuration.

   Either this or ``ip_addrs`` are required keys for a rule.

``action``
   The action describing the behavior of the rule. This can be either ``set_allow`` or ``set_deny``.
   ``set_allow`` provides a list of allowed methods, while all requests with other methods are
   denied. ``set_deny`` provides a list of denied methods, while all requests with other methods are
   allowed. This is a required key.

.. note::
   Prior to |TS| 10.x, these actions were named ``allow`` and ``deny``. In order to bring alignment
   to the action names in remap ACL actions (see :ref:`acl-filters` for more details), these have
   been renamed to ``set_allow`` and ``set_deny``. If
   :ts:cv:`proxy.config.url_remap.acl_behavior_policy` is set to 0, which is the default, the old
   ``allow`` and ``deny`` actions are still supported in order to provide backwards compatibility to
   |TS| 9.x :file:`ip_allow.yaml` files. If it is set to 1, then the use of ``allow`` and ``deny``
   will result in a fatal error with a message asking the user to use ``set_allow`` and ``set_deny``
   instead.

``methods``
   This is optional. If not present, the rule action applies to all methods. If present, the rule
   action is applied to connections using those methods and its opposite to all other connections. The
   keyword "ALL" means all methods, making the specification of any other method redundant. All
   methods comparisons are case insensitive. This is an optional key.

An IP address range for ``ip_addrs`` or ``ip_categories`` can be specified in several ways. A range
is always IPv4 or IPv6, it is not allowed to have a range that contains addresses from different IP
address families.

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
denied. Outbound rules allow by default, so the absence of rules in the default configuration
enables all methods for all outbound connections.

.. note::

   Be aware that ip_allow rules will not, and indeed cannot, be applied to TLS
   connections which are tunneled via ``tunnel_route`` to the upstream target.
   Such connections are not decrypted and thus are not processed by |TS|. This
   applies as well to TLS connections which are forwarded via ``forward_route``
   since, while those are decrypted, they are not processed by |TS|.  For
   details, see :ref:`sni-routing` and :file:`sni.yaml`.

Timing
======

IP allow rules are applied at different stages, depending on the type of rule:

* **On connection accept:** At the beginning of accepting a connection, if the
  source IP address matches an ``in`` rule and the rule denies all methods from
  this client (meaning that all transactions from the client are denied), the
  connection is immediately closed by |TS|. The idea is that since the
  administrator wants to deny all transactions from the client, |TS| should just
  reject any connection from the client and not even process data from it. Note
  that this means that for these all-method rules, no transactions are ever
  processed by |TS|, and thus there will be no transaction log entries for these
  denied connections.
* **Post remap:** ``in`` rules that are not all-method rules (meaning they apply
  only to certain methods) are processed for each transaction after the client
  headers are parsed and remap is applied. Processing the rules post-remap
  allows for remap ACL modifications (see :ref:`Remap ACL Filters
  <acl-filters>`).
* **Preceding server connect:** ``out`` rules are applied after DNS resolution of
  the upstream server but before the connection is established.

Examples
========

The following example enables all clients access::

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

For the purposes of illustration, here is the default configuration in compact form::

   ip_allow: [
     { apply: in, ip_addrs: 127.0.0.1, action: allow },
     { apply: in, ip_addrs: "::1", action: allow },
     { apply: in, ip_addrs: 0/0, action: deny, methods: [ PURGE, PUSH, DELETE, TRACE ] },
     { apply: in, ip_addrs: "::/0", action: deny, methods: [ PURGE, PUSH, DELETE, TRACE ] }
     ]

The following example demonstrates how to use ``ip_categories``. In this example, the
``ip_categories`` is ``ACME_INTERNAL`` which is presumably associated with trusted internal IP
addresses and thus are allowed to ``POST`` and ``DELETE`` resources.

Note this example demonstrates that it is OK to mix ``ip_categories`` and ``ip_addrs`` rules in a
single :file:`ip_allow.yaml` file. In this case all other IPv4 addresses not matched on
``ACME_INTERNAL`` match on ``0/0`` and can only perform ``GET`` and ``HEAD`` requests::

     - apply: in
       ip_categories: ACME_INTERNAL
       action: allow
       methods:
         - GET
         - HEAD
         - POST
         - DELETE
     - apply: in
       ip_addrs: 0/0
       action: allow
       methods:
         - GET
         - HEAD

The set of IP addresses associated with ``ACME_INTERNAL`` can be specified
using the ``ip_categories`` node like so::

     ip_categories:
       - name: ACME_INTERNAL
         ip_addrs:
           - 10.0.0.0/8
           - 172.16.0.0/20
           - 192.168.1.0/24

     ip_allow:
       - apply: in
       # ...

The ``ip_categories`` node will generally be at the start of the :file:`ip_allow.yaml` file.
Alternatively, the same content with the ``ip_categories`` root node can exist in a separate file
specified with the :ts:cv:`proxy.config.cache.ip_categories.filename` configuration.

.. note::

   For ATS 9.0, this file is (almost) backwards compatible. If the first line is a single '#'
   character, or contains only "# ats", then the file will be read in the version 8.0 format. This
   is true for the default format and so if that has not been changed it should still work. This
   allows a grace period before ATS 10.0 which will drop the old format entirely.
