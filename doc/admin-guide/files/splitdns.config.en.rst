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

===============
splitdns.config
===============

.. configfile:: splitdns.config

The :file:`splitdns.config` file enables you to specify the DNS server that
Traffic Server should use for resolving hosts under specific conditions.
For more information, refer to :ref:`admin-split-dns`.

To specify a DNS server, you must supply the following information in
each active line within the file:

-  A primary destination specifier in the form of a destination domain,
   a destination host, or a URL regular expression
-  A set of server directives, listing one or more DNS servers with
   corresponding port numbers

You can also include the following optional information with each DNS
server specification:

-  A default domain for resolving hosts
-  A search list specifying the domain search order when multiple
   domains are specified

After you modify the :file:`splitdns.config` file,
run the :option:`traffic_ctl config reload`
command to apply the changes. When you apply changes to a node in a
cluster, Traffic Server automatically applies the changes to all other
nodes in the cluster.

Format
======

Each line in the :file:`splitdns.config` file uses one of the following
formats: ::

    dest_domain=dest_domain | dest_host | url_regex named=dns_server def_domain=def_domain search_list=search_list

The following list describes each field.

.. _splitdns-config-format-dest-domain:

``dest_domain``
    A valid domain name. This specifies that DNS server selection will
    be based on the destination domain. You can prefix the domain with
    an exclamation mark (``!``) to indicate the NOT logical operator.

.. _splitdns-config-format-dest-host:

``dest_host``
    A valid hostname. This specifies that DNS server selection will be
    based on the destination host. You can prefix the host with an
    exclamation mark (``!``) to indicate the ``NOT`` logical operator.

.. _splitdns-config-format-url-regex:

``url_regex``
    A valid URL regular expression. This specifies that DNS server
    selection will be based on a regular expression.

.. _splitdns-config-format-named:

``named``
    This is a required directive. It identifies the DNS server that
    Traffic Server should use with the given destination specifier. You
    can specify a port using a colon (``:``). If you do not specify a
    port, then 53 is used. Specify multiple DNS servers with spaces or
    semicolons (``;``) as separators.

    You must specify the domains with IP addresses in CIDR ("dot")
    notation.

.. _splitdns-config-format-def-domain:

``def_domain``
    A valid domain name. This optional directive specifies the default
    domain name to use for resolving hosts. Only one entry is allowed.
    If you do not provide the default domain, the system determines its
    value from ``/etc/resolv.conf``

.. _splitdns-config-format-search-list:

``search_list``
    A list of domains separated by spaces or semicolons (;). This
    specifies the domain search order. If you do not provide the search
    list, the system determines the value from :manpage:`resolv.conf(5)`

Examples
========

Consider the following DNS server selection specifications: ::

      dest_domain=internal.company.com named=255.255.255.255:212 255.255.255.254 def_domain=company.com search_list=company.com company1.com
      dest_domain=!internal.company.com named=255.255.255.253

Now consider the following two requests: ::

     http://minstar.internal.company.com

This request matches the first line and therefore selects DNS server
``255.255.255.255`` on port ``212``. All resolver requests use
``company.com`` as the default domain, and ``company.com`` and
``company1.com`` as the set of domains to search first. ::

     http://www.microsoft.com

This request matches the second line. Therefore, Traffic Server selects
DNS server ``255.255.255.253``. Because no ``def_domain`` or
``search_list`` was supplied, Traffic Server retrieves this information
from :manpage:`resolv.conf(5)`

