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

.. include:: ../common.defs

.. _admin-introduction:

Introduction
************

Global data networking has become part of everyday life: Internet users
request billions of documents and petabytes of data, on a daily basis,
to and from all parts of the world. Information is free, abundant, and
accessible. Unfortunately, global data networking can also be a
nightmare for IT professionals as they struggle with overloaded servers
and congested networks. It can be challenging to consistently and
reliably accommodate society's growing data demands.

|TS| is a high-performance web proxy cache that improves
network efficiency and performance by caching frequently-accessed
information at the edge of the network. This brings content physically
closer to end users, while enabling faster delivery and reduced
bandwidth use. |TS| is designed to improve content delivery
for enterprises, Internet service providers (ISPs), backbone providers,
and large intranets by maximizing existing and available bandwidth.

|TS| Deployment Options
=======================

To best suit your needs, |TS| can be deployed in several ways:

-  As a web proxy cache
-  As a reverse proxy
-  In a cache hierarchy

The following sections provide a summary of these |TS|
deployment options.

|TS| as a Web Proxy Cache
-------------------------

As a web proxy cache, |TS| receives user requests for web
content as those requests travel to the destined web server (origin
server). If |TS| contains the requested content, then it
serves the content directly. If the requested content is not available
from cache, then |TS| acts as a proxy: it obtains the content
from the origin server on the user's behalf and also keeps a copy to
satisfy future requests.

|TS| provides explicit proxy caching, in which the user's
client software must be configured to send requests directly to Traffic
Server. Explicit proxy caching is described in the :ref:`explicit-proxy-caching`
chapter.

|TS| can also be employed as a transparent caching proxy server, in
which the client software needs no special configuration or even knowledge of
the proxy's existence. This setup is described in the :ref:`transparent-proxy`
section.

|TS| as a Reverse Proxy
-----------------------

As a reverse proxy, |TS| is configured to be the origin server
to which the user is trying to connect (typically, the origin server's
advertised hostname resolves to |TS|, which acts as the real
origin server). The reverse proxy feature is also called server
acceleration. Reverse proxy is described in more detail in
:ref:`reverse-proxy-and-http-redirects`.

|TS| in a Cache Hierarchy
-------------------------

|TS| can participate in flexible cache hierarchies, in which
Internet requests not fulfilled from one cache are routed to other
regional caches, thereby leveraging the contents and proximity of nearby
caches. In a hierarchy of proxy servers, |TS| can act either
as a parent or a child cache to other |TS| systems or to
similar caching products.

|TS| as a Load Balancer
-----------------------

|TS| can act as a layer 7 HTTP load balancer distributing requests across
several servers. It can choose the next hop server using request attributes
like the Host: header, URL attributes, scheme, method, and client IP address.
It has a few selection strategies in place like weighted round robin, and
URL consistent hashing.

|TS| Components
===============

|TS| consists of several components that work together to form
a web proxy cache you can easily monitor and configure.

The |TS| Cache
--------------

The |TS| cache consists of a high-speed object database called
the *object store*. The object store indexes objects according to URLs and
associated headers. Using sophisticated object management, the object
store can cache alternate versions of the same object (perhaps in a
different language or encoding type). It can also efficiently store very
small and very large objects, thereby minimizing wasted space. When the
cache is full, |TS| removes stale data to ensure that the most
requested objects are readily available and fresh.

|TS| is designed to tolerate total disk failures on any of the
cache disks. If the disk fails completely, then |TS| marks the
entire disk as corrupt and continues to use remaining disks. If all of
the cache disks fail, then |TS| switches to proxy-only mode.
You can partition the cache to reserve a certain amount of disk space
for storing data for specific protocols and origin servers. For more
information about the cache, see :ref:`http-proxy-caching`.

The RAM Cache
-------------

|TS| maintains a small RAM cache that contains extremely
popular objects. This RAM cache serves the most popular objects as fast
as possible and reduces load on disks, especially during temporary
traffic peaks. You can configure the RAM cache size to suit your needs.
For detailed information, refer to :ref:`changing-the-size-of-the-ram-cache`.

The Host Database
-----------------

The |TS| host database stores the domain name server (DNS)
entries of origin servers to which |TS| connects to fulfill
user requests. This information is used to adapt future protocol
interactions and optimize performance. Along with other information, the
host database tracks:

-  DNS information (for fast conversion of hostnames to IP addresses).

-  The HTTP version of each host (so advanced protocol features can be
   used with hosts running modern servers).

-  Host reliability and availability information (so users will not wait
   for servers that are not running).

The DNS Resolver
----------------

|TS| includes a fast, asynchronous DNS resolver to streamline
conversion of hostnames to IP addresses. |TS| implements the
DNS resolver natively by directly issuing DNS command packets rather
than relying on slower, conventional resolver libraries. Since many DNS
queries can be issued in parallel and a fast DNS cache maintains popular
bindings in memory, DNS traffic is reduced.

|TS| Processes
--------------

|TS| contains a single processes to serve requests, manage administrative
calls(JSONRPC) and handle configuration.

#. The :program:`traffic_server` process is the transaction processing engine
   of |TS|. It is responsible for accepting connections,
   processing protocol requests, and serving documents from the cache or
   origin server.

Administration Tools
--------------------

|TS| offers the following administration options:

-  The :program:`traffic_ctl` command-line interface is a
   text-based interface from which you can monitor |TS| performance
   and network traffic, as well as configure the |TS| system.

-  Various configuration files enable you to configure |TS|
   through a simple file-editing and signal-handling interface. Any
   changes you make through :program:`traffic_ctl` are
   automatically made to the configuration files as well.

-  Finally, there is a JSONRPC 2.0 interface which provides access to the JSONRPC 2.0
   Administrative endpoint which allow you to implement your own tool by just using
   JSON or YAML. Check :ref:`jsonrpc-node` for more information.

Traffic Analysis Options
========================

|TS| provides several options for network traffic analysis and
monitoring:

-  :program:`traffic_ctl` enables you to collect and process
   statistics obtained from network traffic information.

-  Transaction logging enables you to record information (in a log file)
   about every request |TS| receives and every error it
   detects. By analyzing the log files, you can determine how many
   clients used the |TS| cache, how much information each of
   them requested, and what pages were most popular. You can also see
   why a particular transaction was in error and what state the Traffic
   Server was in at a particular time. For example, you can see that
   |TS| was restarted.

   |TS| supports several standard log file formats, such as
   Squid and Netscape, and its own custom format. You can analyze the
   standard format log files with off-the-shelf analysis packages. To
   help with log file analysis, you can separate log files so that they
   contain information specific to protocol or hosts.

|TS| event and error logging, monitoring, and analysis is covered in greater
detail in :ref:`admin-monitoring`.

|TS| Security Options
=====================

|TS| provides numerous options that enable you to establish
secure communication between the |TS| system and other
computers on the network. Using the security options, you can do the
following:

-  Control client access to the |TS| proxy cache.

-  Configure |TS| to use multiple DNS servers to match your
   site's security configuration. For example, |TS| can use
   different DNS servers, depending on whether it needs to resolve
   hostnames located inside or outside a firewall. This enables you to
   keep your internal network configuration secure while continuing to
   provide transparent access to external sites on the Internet.

-  Configure |TS| to verify that clients are authenticated
   before they can access content from the |TS| cache.

-  Secure connections in reverse proxy mode between a client and Traffic
   Server, and |TS| and the origin server, using the SSL
   termination option.

-  Control access via SSL (Secure Sockets Layer).

|TS| security options are described in more detail in
:ref:`admin-security`.

Tuning |TS|
===========

Finally, this last chapter on :ref:`performance-tuning` discusses the vast
number of options that allow administrators to optimally tune Apache Traffic
Server for maximum performance.
