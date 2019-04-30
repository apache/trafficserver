.. _reverse-proxy-and-http-redirects:

Reverse Proxy and HTTP Redirects
********************************

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

Reverse Proxy and HTTP Redirects
================================

As a reverse proxy cache, Traffic Server serves requests on behalf of
origin servers. Traffic Server is configured in such a way that it
appears to clients like a normal origin server.

.. toctree::
   :maxdepth: 2

Understanding Reverse Proxy Caching
===================================

With *forward proxy caching*, Traffic Server handles web requests to origin
servers on behalf of the clients requesting the content. *Reverse proxy
caching* (also known as *server acceleration*) is different because Traffic
Server acts as a proxy cache on behalf of the origin servers that store the
content. Traffic Server is configured to behave outwardly as origin server
which the client is trying to connect to. In a typical scenario the advertised
hostname of the origin server resolves to Traffic Server, which serves client
requests directly, fetching content from the true origin server when necessary.

Reverse Proxy Solutions
-----------------------

There are many ways to use Traffic Server as a reverse proxy. Below are
a few example scenarios.

-  Offload heavily-used origin servers.

-  Deliver content efficiently in geographically distant areas.

-  Provide security for origin servers that contain sensitive information.

Offloading Heavily-Used Origin Servers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Traffic Server can accept requests on behalf of the origin server and improve
the speed and quality of web serving by reducing load and hot spots on
backup origin servers. For example, a web host can maintain a scalable
Traffic Server system with a set of low-cost, low-performance,
less-reliable PC origin servers as backup servers. In fact, a single
Traffic Server can act as the virtual origin server for multiple backup
origin servers, as shown in the figure below.

.. figure:: /static/images/admin/revproxy.jpg
   :align: center
   :alt: Traffic Server as reverse proxy for a pair of origin servers

   Traffic Server as reverse proxy for a pair of origin servers

Delivering Content in Geographically-Dispersed Areas
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Traffic Server can be used in reverse proxy mode to accelerate origin
servers that provide content to areas not located within close
geographical proximity. Caches are typically easier to manage and are
more cost-effective than replicating data. For example, Traffic Server
can be used as a mirror site on the far side of a trans-Atlantic link to
serve users without having to fetch the request and content across
expensive, or higher latency, international connections. Unlike replication,
for which hardware must be configured to replicate all data and to handle peak
capacity, Traffic Server dynamically adjusts to optimally use the
serving and storing capacity of the hardware. Traffic Server is also
designed to keep content fresh automatically, thereby eliminating the
complexity of updating remote origin servers.

Providing Security for an Origin Server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Traffic Server can be used in reverse proxy mode to provide security for
an origin server. If an origin server contains sensitive information
that you want to keep secure inside your firewall, then you can use a
Traffic Server outside the firewall as a reverse proxy for that origin
server. When outside clients try to access the origin server, the
requests instead go to Traffic Server. If the desired content is not
sensitive, then it can be served from the cache. If the content is
sensitive and not cacheable, then Traffic Server obtains the content
from the origin server (the firewall allows only Traffic Server access
to the origin server). The sensitive content resides on the origin
server, safely inside the firewall.

How Does Reverse Proxy Work?
----------------------------

When a browser makes a request, it normally sends that request directly
to the origin server. When Traffic Server is in reverse proxy mode, it
intercepts the request before it reaches the origin server. Typically,
this is done by setting up the DNS entry for the origin server (i.e.,
the origin server's advertised hostname) so it resolves to the Traffic
Server IP address. When Traffic Server is configured as the origin
server, the browser connects to Traffic Server rather than the origin
server. For additional information, see `HTTP Reverse Proxy`_.

.. note::

    To avoid a DNS conflict, the origin server’s hostname and its advertised
    hostname must not be the same.

HTTP Reverse Proxy
==================

In reverse proxy mode, Traffic Server serves HTTP requests on behalf of
a web server. The figure below illustrates how Traffic Server in reverse
proxy mode serves an HTTP request from a client browser.

.. figure:: /static/images/admin/httprvs.jpg
   :align: center
   :alt: HTTP reverse proxy

   HTTP reverse proxy

The figure above demonstrates the following steps:

.. List below should remain manually numbered, as the entries correspond to numbers in the figure above.

1. A client browser sends an HTTP request addressed to a host called
   ``www.host.com`` on port 80. Traffic Server receives the request
   because it is acting as the origin server (the origin server’s
   advertised hostname resolves to Traffic Server).

2. Traffic Server locates a map rule in the :file:`remap.config` file and
   remaps the request to the specified origin server (``realhost.com``).

3. If the request cannot be served from cache, Traffic Server opens a
   connection to the origin server (or more likely, uses an existing
   connection it has pre-established), retrieves the content, and optionally
   caches it for future use.

4. If the request was a cache hit and the content is still fresh in the cache,
   or the content is now available through Traffic Server because of step 3,
   Traffic Server sends the requested object to the client from the cache
   directly.

.. note::

    Traffic Server, when updating its own cache from the origin server, will
    simultaneously deliver that content to the client while updating its
    cache database. The response to the client containing the requested object
    will begin as soon as Traffic Server has received and processed the full
    response headers from the origin server.

To configure HTTP reverse proxy, you must perform the following tasks:

-  Create mapping rules in the :file:`remap.config` file (refer to `Creating
   Mapping Rules for HTTP Requests`_). ::

      map http://www.host.com http://realhost.com

-  Enable the reverse proxy option (refer to `Enabling HTTP Reverse Proxy`_).

In addition to the tasks above, you can also `Setting Optional HTTP Reverse Proxy Options`_.

Handling Origin Server Redirect Responses
-----------------------------------------

Origin servers often send redirect responses back to browsers
redirecting them to different pages. For example, if an origin server is
overloaded, then it might redirect browsers to a less loaded server.
Origin servers also redirect when web pages have moved to different
locations. When Traffic Server is configured as a reverse proxy, it must
readdress redirects from origin servers so that browsers are redirected
to Traffic Server and not to another origin server.

To readdress redirects, Traffic Server uses reverse-map rules. Unless
you have :ts:cv:`proxy.config.url_remap.pristine_host_hdr` enabled
(the default) you should generally set up a reverse-map rule for
each map rule. To create reverse-map rules, refer to `Using Mapping
Rules for HTTP Requests`_.

Using Mapping Rules for HTTP Requests
-------------------------------------

Traffic Server uses two types of mapping rules for HTTP reverse proxy.

map rule
~~~~~~~~

A *map rule* translates the URL in client requests into the URL where
the content is located. When Traffic Server is in reverse proxy mode and
receives an HTTP client request, it first constructs a complete request
URL from the relative URL and its headers. Traffic Server then looks for
a match by comparing the complete request URL with its list of target
URLs in :file:`remap.config`. For the request URL to match a target URL, the
following conditions must be true:

-  The scheme of both URLs must be the same.

-  The host in both URLs must be the same. If the request URL contains
   an unqualified hostname, then it will never match a target URL with a
   fully-qualified hostname.

-  The ports in both URLs must be the same. If no port is specified in a
   URL, then the default port for the scheme of the URL is used.

-  The path portion of the target URL must match a prefix of the request
   URL path.

If Traffic Server finds a match, then it translates the request URL into
the replacement URL listed in the map rule: it sets the host and path of
the request URL to match the replacement URL. If the URL contains path
prefixes, then Traffic Server removes the prefix of the path that
matches the target URL path and substitutes it with the path from the
replacement URL. If two mappings match a request URL, then Traffic
Server applies the first mapping listed in :file:`remap.config`.

reverse-map rule
~~~~~~~~~~~~~~~~

A *reverse-map rule* translates the URL in origin server redirect
responses to point to Traffic Server so that clients are redirected
to Traffic Server instead of accessing an origin server directly. For
example, if there is a directory ``/pub`` on an origin server at
``www.molasses.com`` and a client sends a request to that origin server
for ``/pub``, then the origin server might reply with a redirect by
sending the Header ``Location: http://realhost.com/pub/`` to let the
client know that it was a directory it had requested, not a document (a
common use of redirects is to normalize URLs so that clients can
bookmark documents properly).

Traffic Server uses ``reverse_map`` rules to prevent clients (that
receive redirects from origin servers) from bypassing Traffic Server and
directly accessing the origin servers. In many cases the client would be
hitting a wall because ``realhost.com`` actually does not resolve for
the client. (E.g.: Because it's running on a port shielded by a
firewall, or because it's running on a non-routable LAN IP)

Both map and reverse-map rules consist of a *target* (origin) URL and
a *replacement* (destination) URL. In a *map rule*, the target URL
points to Traffic Server and the replacement URL specifies where the
original content is located. In a *reverse-map rule*, the target URL
specifies where the original content is located and the replacement URL
points to Traffic Server. Traffic Server stores mapping rules in
:file:`remap.config` located in the Traffic Server ``config`` directory.

Creating Mapping Rules for HTTP Requests
----------------------------------------

To create mapping rules:

#. Enter the map and reverse-map rules into :file:`remap.config`.

#. Run the command :option:`traffic_ctl config reload` to apply the configuration changes.

Enabling HTTP Reverse Proxy
---------------------------

To enable HTTP reverse proxy:

#. Edit :ts:cv:`proxy.config.reverse_proxy.enabled` in :file:`records.config`. ::

    CONFIG proxy.config.reverse_proxy.enabled INT 1

#. Run the command :option:`traffic_ctl config reload` to apply the configuration changes.

Setting Optional HTTP Reverse Proxy Options
-------------------------------------------

Traffic Server provides several reverse proxy configuration options in
:file:`records.config` that enable you to:

-  Configure Traffic Server to retain the client host header information
   in a request during translation.
   See :ts:cv:`proxy.config.url_remap.pristine_host_hdr`.

-  Configure Traffic Server to serve requests only to the origin servers
   listed in the mapping rules. As a result, requests to origin servers
   not listed in the mapping rules are not served.
   See :ts:cv:`proxy.config.url_remap.remap_required`.

-  Specify an alternate URL to which incoming requests from older clients ,such
   as ones that do not provide ``Host`` headers, are directed.
   See :ts:cv:`proxy.config.header.parse.no_host_url_redirect`.

Run the command :option:`traffic_ctl config reload` to apply any of these configuration
changes.

Redirecting HTTP Requests
=========================

You can configure Traffic Server to redirect HTTP requests without
having to contact any origin servers. For example, if you redirect all
requests for ``http://www.ultraseek.com`` to
``http://www.server1.com/products/portal/search/``, then all HTTP
requests for ``www.ultraseek.com`` go directly to
``www.server1.com/products/portal/search``.

You can configure Traffic Server to perform permanent or temporary
redirects. *Permanent redirects* notify the browser of the URL change
(by returning the HTTP status code ``301``) so that the browser can
update bookmarks. *Temporary redirects* notify the browser of the URL
change for the current request only (by returning the HTTP status code
``307`` ).

To set redirect rules:

#. For each redirect you want to set enter a mapping rule in :file:`remap.config`.

#. Run the command :option:`traffic_ctl config reload` to apply the configuration changes.

Example
-------

The following permanently redirects all HTTP requests for
``www.server1.com`` to ``www.server2.com``: ::

    redirect http://www.server1.com http://www.server2.com


