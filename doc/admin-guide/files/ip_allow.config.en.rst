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
.. highlight:: none

===============
ip_allow.config
===============

.. configfile:: ip_allow.config

The :file:`ip_allow.config` file controls client access to |TS| and |TS| connections to the servers.
You can specify ranges of IP addresses that are allowed to connect to |TS| or that are allowed to be
remapped by Traffic Server. After you modify the :file:`ip_allow.config` file, navigate to the |TS|
bin directory and run the :option:`traffic_ctl config reload` command to apply changes.

Format
======

Each line in :file:`ip_allow.config` file must have on of the following formats
format::

    src_ip=<range of IP addresses> action=<action> [method=<list of methods separated by '|'>]
    dest_ip=<range of IP addresses> action=<action> [method=<list of methods separated by '|'>]

For ``src_ip`` the remote inbound connection address, i.e. the IP address of the client, is checked
against the specified range of IP addresses. For ``dest_ip`` the outbound remote address (i.e. the IP
address to which |TS| connects) is checked against the specified IP address range.

Range specifications can be IPv4 or IPv6, but any single range must be one or the other. Ranges can
be specified by two addresses, the lower address and the upper address, separated by a dash, ``-``.
Such a range inclusive and contains the lower, upper addresses and all addresses inbetween. A range
can also be specified by an address and a CIDR mask, separated by a slash, ``/``. This case is
converted to a range of the previous case by retaining only the left most ``mask`` bits, clearing
the rest for the lower address and setting them for the upper address. For instance, a mask of
``23`` would mean the left most 23 bits are kept and all bits to the right are cleared or set.
Finally, a range can be a single IP address which matches exactly that address (the equivalent of a
range with the lower and upper values equal to that IP address).

The value of ``method`` is a string which must consist of either HTTP method names separated by the
character '|' or the keyword literal ``ALL``. This keyword may omitted in which case it is treated
as if it were ``method=ALL``. Methods can also be specified by having multiple instances of the
``method`` keyword, each specifiying a single method. E.g., ``method=GET|HEAD`` is the same as
``method=GET method=HEAD``. The method names are not validated which means non-standard method names
can be specified.

The ``action`` must be either ``ip_allow`` or ``ip_deny``. This controls what |TS| does if the
address is in the range and the method matches. If there is a match, |TS| allows the connection (for
``ip_allow``) or denies it (``ip_deny``).

For each inbound or outbound connection the applicable rule is selectd by first match on the IP
address. The rule is then applied (if the method matches) or its opposite is applied (if the method
doesn't match). If no rule is matched access is allowed. This makes each rule both an accept and
deny, one explicit and the other implicit. The ``src_ip`` rules are checked when a host connects
to |TS|. The ``dest_ip`` rules are checked when |TS| connects to another host.

By default the :file:`ip_allow.config` file contains the following lines, which allows all methods
to connections from localhost and denies the ``PUSH``, ``PURGE`` and ``DELETE`` methods to all other
IP addresses (note this allows all other methods to all IP addresses)::

    src_ip=127.0.0.1                                  action=ip_allow method=ALL
    src_ip=::1                                        action=ip_allow method=ALL
    src_ip=0.0.0.0-255.255.255.255                    action=ip_deny  method=PUSH|PURGE|DELETE
    src_ip=::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff action=ip_deny  method=PUSH|PURGE|DELETE

This could also be specified as::

    src_ip=127.0.0.1   action=ip_allow method=ALL
    src_ip=::1         action=ip_allow method=ALL
    src_ip=0/0         action=ip_deny  method=PUSH|PURGE|DELETE
    src_ip=::/0        action=ip_deny  method=PUSH|PURGE|DELETE

Examples
========

The following example enables all clients access.::

   src_ip=0.0.0.0-255.255.255.255 action=ip_allow

The following example allows access to all clients on addresses in a subnet::

   src_ip=123.12.3.000-123.12.3.123 action=ip_allow

The following example denies access all clients on addresses in a subnet::

   src_ip=123.45.6.0-123.45.6.123 action=ip_deny

If the entire subnet were to be denied, that would be::

   src_ip=123.45.6.0/24 action=ip_deny

The following example allows to any upstream servers::

   dest_ip=0.0.0.0-255.255.255.255 action=ip_allow

Alternatively this can be done with::

   dest_ip=0/0 action=ip_allow

The following example denies to access all servers on a specific subnet::

   dest_ip=10.0.0.0-10.0.255.255 action=ip_deny

Alternatively::

   dest_ip=10.0.0.0/16 action=ip_deny

If the goal is to allow only ``GET`` and ``HEAD`` requests to those servers, it would be::

   dest_ip=10.0.0.0/16 action=ip_allow method=GET method=HEAD

or::

   dest_ip=10.0.0.0/16 action=ip_allow method=GET|HEAD

This will match the IP address for the targer servers on the outbound connection. Then, if the
method is ``GET`` or ``HEAD`` the connection will be allowed, otherwise the connection will be
denied.

