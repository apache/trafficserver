.. _admin-guide:

Administrators' Guide
**********************

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


Apache Traffic Server™ speeds Internet access, enhances website
performance, and delivers unprecedented web hosting capabilities.

This chapter discusses how:

Contents:

.. toctree::
   :maxdepth: 2

   getting-started.en
   http-proxy-caching.en
   reverse-proxy-http-redirects.en
   forward-proxy.en
   transparent-proxy.en
   explicit-proxy-caching.en
   session-protocol.en
   hierachical-caching.en
   configuring-cache.en
   monitoring-traffic.en
   configuring-traffic-server.en
   cluster-howto.en
   security-options.en
   working-log-files.en
   event-logging-formats.en
   traffic-server-error-messages.en
   performance-tuning.en
   faqs.en


What Is Apache Traffic Server?
==============================

Global data networking has become part of everyday life: Internet users
request billions of documents and terabytes of data, on a daily basis,
to and from all parts of the world. Information is free, abundant, and
accessible. Unfortunately, global data networking can also be a
nightmare for IT professionals as they struggle with overloaded servers
and congested networks. It can be challenging to consistently and
reliably accommodate society’s growing data demands.

Traffic Server is a high-performance web proxy cache that improves
network efficiency and performance by caching frequently-accessed
information at the edge of the network. This brings content physically
closer to end users, while enabling faster delivery and reduced
bandwidth use. Traffic Server is designed to improve content delivery
for enterprises, Internet service providers (ISPs), backbone providers,
and large intranets by maximizing existing and available bandwidth.

Traffic Server Deployment Options
=================================

To best suit your needs, Traffic Server can be deployed in several ways:

-  As a web proxy cache
-  As a reverse proxy
-  In a cache hierarchy

The following sections provide a summary of these Traffic Server
deployment options. Please keep in mind that with every of these options
Traffic Server can be run as a *single instance*, or as a *multi-node
cluster*.

Traffic Server as a Web Proxy Cache
-----------------------------------

As a web proxy cache, Traffic Server receives user requests for web
content as those requests travel to the destined web server (origin
server). If Traffic Server contains the requested content, then it
serves the content directly. If the requested content is not available
from cache, then Traffic Server acts as a proxy: it obtains the content
from the origin server on the user’s behalf and also keeps a copy to
satisfy future requests.

Traffic Server provides explicit proxy caching, in which the user’s
client software must be configured to send requests directly to Traffic
Server. Explicit proxy caching is described in the :ref:`explicit-proxy-caching`
chapter.


Traffic Server as a Reverse Proxy
---------------------------------

As a reverse proxy, Traffic Server is configured to be the origin server
to which the user is trying to connect (typically, the origin server’s
advertised hostname resolves to Traffic Server, which acts as the real
origin server). The reverse proxy feature is also called server
acceleration. Reverse proxy is described in more detail in :ref:`reverse-proxy-and-http-redirects`.

Traffic Server in a Cache Hierarchy
-----------------------------------

Traffic Server can participate in flexible cache hierarchies, in which
Internet requests not fulfilled from one cache are routed to other
regional caches, thereby leveraging the contents and proximity of nearby
caches. In a hierarchy of proxy servers, Traffic Server can act either
as a parent or a child cache to other Traffic Server systems or to
similar caching products.

Traffic Server supports ICP (Internet Cache Protocol) peering.
Hierarchical caching is described in more detail in :ref:`hierarchical-caching`.

Deployment Limitations
----------------------

There's a number of deployment options that Traffic Server does not support right out
of the box. Such funcionality may be implemented in a plugin, but in some cases
Traffic Server's internal APIs or architectural restrictions won't make it easy:

* Load Balancing - note that there is an experimental plugin for this: :ref:`balancer-plugin`.

Traffic Server Components
=========================

Traffic Server consists of several components that work together to form
a web proxy cache you can easily monitor and configure. These main
components are described below.

The Traffic Server Cache
------------------------

The Traffic Server cache consists of a high-speed object database called
the object store. The object store indexes objects according to URLs and
associated headers. Using sophisticated object management, the object
store can cache alternate versions of the same object (perhaps in a
different language or encoding type). It can also efficiently store very
small and very large objects, thereby minimizing wasted space. When the
cache is full, Traffic Server removes stale data to ensure that the most
requested objects are readily available and fresh.

Traffic Server is designed to tolerate total disk failures on any of the
cache disks. If the disk fails completely, then Traffic Server marks the
entire disk as corrupt and continues to use remaining disks. If all of
the cache disks fail, then Traffic Server switches to proxy-only mode.
You can partition the cache to reserve a certain amount of disk space
for storing data for specific protocols and origin servers. For more
information about the cache, see :ref:`configuring-the-cache`.

The RAM Cache
-------------

Traffic Server maintains a small RAM cache that contains extremely
popular objects. This RAM cache serves the most popular objects as fast
as possible and reduces load on disks, especially during temporary
traffic peaks. You can configure the RAM cache size to suit your needs;
for detailed information, refer to :ref:`changing-the-size-of-the-ram-cache`.

The Host Database
-----------------

The Traffic Server host database stores the domain name server (DNS)
entries of origin servers to which Traffic Server connects to fulfill
user requests. This information is used to adapt future protocol
interactions and optimize performance. Along with other information, the
host database tracks:

-  DNS information (for fast conversion of hostnames to IP addresses)
-  The HTTP version of each host (so advanced protocol features can be
   used with hosts running modern servers)
-  Host reliability and availability information (so users will not wait
   for servers that are not running)

The DNS Resolver
----------------

Traffic Server includes a fast, asynchronous DNS resolver to streamline
conversion of hostnames to IP addresses. Traffic Server implements the
DNS resolver natively by directly issuing DNS command packets rather
than relying on slower, conventional resolver libraries. Since many DNS
queries can be issued in parallel and a fast DNS cache maintains popular
bindings in memory, DNS traffic is reduced.

Traffic Server Processes
------------------------

Traffic Server contains three processes that work together to serve
requests and manage/control/monitor the health of the system. The three
processes are described below:

-  The :program:`traffic_server` process is the transaction processing engine
   of Traffic Server. It is responsible for accepting connections,
   processing protocol requests, and serving documents from the cache or
   origin server.

-  The :program:`traffic_manager` process is the command and control facility
   of the Traffic Server, responsible for launching, monitoring, and
   reconfiguring the :program:`traffic_server` process. The :program:`traffic_manager`
   process is also responsible for the proxy autoconfiguration port, the
   statistics interface, cluster administration, and virtual IP
   failover.

   If the :program:`traffic_manager` process detects a :program:`traffic_server`
   process failure, it instantly restarts the process but also maintains
   a connection queue of all incoming requests. All incoming connections
   that arrive in the several seconds before full server restart are
   saved in the connection queue and processed in first-come,
   first-served order. This connection queueing shields users from any
   server restart downtime.

-  The :program:`traffic_cop` process monitors the health of both the
   :program:`traffic_server` and :program:`traffic_manager` processes. The
   :program:`traffic_cop` process periodically (several times each minute)
   queries the :program:`traffic_server` and :program:`traffic_manager` process by
   issuing heartbeat requests to fetch synthetic web pages. In the event
   of failure (if no response is received within a timeout interval or
   if an incorrect response is received), :program:`traffic_cop` restarts the
   :program:`traffic_manager` and :program:`traffic_server` processes.

The figure below illustrates the three Traffic Server processes.

.. figure:: ../static/images/admin/process.jpg
   :align: center
   :alt: Illustration of the three Traffic Server Processes

   Illustration of the three Traffic Server Processes

Administration Tools
--------------------

Traffic Server offers the following administration options:

-  The Traffic Line command-line interface is a text-based interface
   from which you can monitor Traffic Server performance and network
   traffic, as well as configure the Traffic Server system. From Traffic
   Line, you can execute individual commands or script a series of
   commands in a shell.
-  The Traffic Shell command-line interface is an additional
   command-line tool that enables you to execute individual commands
   that monitor and configure the Traffic Server system.
-  Various configuration files enable you to configure Traffic Server
   through a simple file-editing and signal-handling interface. Any
   changes you make through Traffic Line or Traffic Shell are
   automatically made to the configuration files as well.
-  Finally there is a clean C API which can be put to good use from a
   multitude of languages. The Traffic Server Admin Client demonstrates
   this for Perl.

Traffic Analysis Options
========================

Traffic Server provides several options for network traffic analysis and
monitoring:

-  Traffic Line and Traffic Shell enable you to collect and process
   statistics obtained from network traffic information.

-  Transaction logging enables you to record information (in a log file)
   about every request Traffic Server receives and every error it
   detects. By analyzing the log files, you can determine how many
   clients used the Traffic Server cache, how much information each of
   them requested, and what pages were most popular. You can also see
   why a particular transaction was in error and what state the Traffic
   Server was in at a particular time; for example, you can see that
   Traffic Server was restarted or that cluster communication timed out.

   Traffic Server supports several standard log file formats, such as
   Squid and Netscape, and its own custom format. You can analyze the
   standard format log files with off-the-shelf analysis packages. To
   help with log file analysis, you can separate log files so that they
   contain information specific to protocol or hosts.

Traffic analysis options are described in more detail in :ref:`monitoring-traffic`.

Traffic Server logging options are described in :ref:`working-with-log-files`.

Traffic Server Security Options
===============================

Traffic Server provides numerous options that enable you to establish
secure communication between the Traffic Server system and other
computers on the network. Using the security options, you can do the
following:

-  Control client access to the Traffic Server proxy cache.
-  Configure Traffic Server to use multiple DNS servers to match your
   site's security configuration. For example, Traffic Server can use
   different DNS servers, depending on whether it needs to resolve
   hostnames located inside or outside a firewall. This enables you to
   keep your internal network configuration secure while continuing to
   provide transparent access to external sites on the Internet.
-  Configure Traffic Server to verify that clients are authenticated
   before they can access content from the Traffic Server cache.
-  Secure connections in reverse proxy mode between a client and Traffic
   Server, and Traffic Server and the origin server, using the SSL
   termination option.
-  Control access via SSL (Secure Sockets Layer).

Traffic Server security options are described in more detail in
:ref:`security-options`.

Tuning Traffic Server
=====================

Finally this last chapter on :ref:`performance-tuning` discusses the vast
number of options that allow to optimally tune Apache Traffic Server for
maximum performance.
