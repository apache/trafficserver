.. _forward-proxy:

Forward Proxy
*************

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

The Apache Traffic Server is a general purpose *proxy*. As such it can
also be used as forward proxy.

A forward proxy is can be used as a central tool in your infrastructure
to access the web. In combination with a cache that means overall
reduced bandwidth usage.

If your forward proxy is not also configured as :ref:`transparent-proxy`
your clients will have to be configured to actually use it.

The main difference between a forward and a transparent proxy is that
User Agents *know* that they are accessing a proxy, thus forming their
requests like so: ::

    GET http://example.com/index.php?id=1337 HTTP/1.1

This request, then is translated by the proxy to::

    GET /index?id=1337 HTTP/1.1
    Host: example.com

Apache Traffic Server offers two ways to User Agents: They can either be
pointed directly to the default ``8080`` port. Alternatively, they can
be pointed to the more dynamic :ts:cv:`proxy.config.url_remap.default_to_server_pac`

This port will then serve a JavaScript like configuration that User
Agents can use to determine where to send their requests to.

Configuration
=============

In order to configure Apache Traffic Server as forward proxy you will
have to edit :file:`records.config` and set

-  :ts:cv:`proxy.config.url_remap.remap_required` to  ``0``

If your proxy is serving as *pure* forward proxy, you will also want to
set

-  :ts:cv:`proxy.config.reverse_proxy.enabled` to  ``0``

Other configuration variables to consider:

-  :ts:cv:`proxy.config.http.no_dns_just_forward_to_parent`
-  :ts:cv:`proxy.config.http.forward.proxy_auth_to_parent`
-  :ts:cv:`proxy.config.http.insert_squid_x_forwarded_for`

Security Considerations
=======================

It's important to note that once your Apache Traffic Server is
configured as forward proxy it will indiscriminately accept proxy
requests from anyone. That means, if it's reachable on the internet, you
have configured an *Open Proxy*. Most of the time, this is *not* what
you want, so you'll have to make sure it's either only reachable within
your NAT or is secured by firewall rules that permit only those clients
to access it which you want to it to access.

