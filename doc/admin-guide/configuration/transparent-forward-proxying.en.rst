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

The Apache Traffic Server is a general purpose proxy, configurable as both a
reverse and forward proxy.

A forward proxy can be used as a central tool in your infrastructure
to access the web and it may be combined with a cache to lower your overall
bandwidth usage. Forward proxies act as a gatekeeper between client browsers
on your local network and all (or some, at your configuration's discretion)
web sites accessed by those clients. The forward proxy will receive the
HTTP requests, perform any filtering or request alteration rules you
establish, and when appropriate forward the request on to its destination
website. The response will return through your proxy, where it may optionally
be cached and/or modified, and then returned to the original client.

There are two modes in which your forward proxy may operate:

Forward Proxy
    Each client must be configured explicitly to use the forward proxy. Client
    browsers will be aware of the fact they are using a proxy and will form their
    HTTP requests appropriately. This results in the initial HTTP command being
    issued with fully qualified URIs that contain the destination hostname::

        GET http://example.com/index.php?id=123 HTTP/1.1

Transparent Proxy
    The use of a transparent proxy is typically done in concert with network
    routing rules which redirect all outbound HTTP traffic through your proxy.
    Clients will behave, and form their HTTP requests, as if they are contacting
    the remote site directly, and will not be aware of the existence of a proxy
    server in between themselves and the remote servers. HTTP requests will be
    generated per their usual form, with only paths in the command and a
    separate Host request header::

        GET /index?id=123 HTTP/1.1
        Host: example.com

Apache Traffic Server may be configured to operate as both a forward and
a transparent proxy simultaneously.

Proxy Configuration
===================

Configuring basic forward proxy operation in Traffic Server is quite simple
and straightforward.

1. Permit Traffic Server to process requests for hosts not explicitly configured
   in the remap rules, by modifying :ts:cv:`proxy.config.url_remap.remap_required`
   in :file:`records.yaml`::

        CONFIG proxy.config.url_remap.remap_required INT 0

2. *Optional*: If Traffic Server will be operating strictly as a forward proxy,
   you will want to disable reverse proxy support by modifying
   :ts:cv:`proxy.config.reverse_proxy.enabled` in :file:`records.yaml`::

        CONFIG proxy.config.reverse_proxy.enabled INT 0

You may also want to consider some of these configuration options:

- Setting :ts:cv:`proxy.config.http.no_dns_just_forward_to_parent` determines which
  host will be used for DNS resolution.

- Proxy Authentication can be enabled or disabled with
  :ts:cv:`proxy.config.http.forward.proxy_auth_to_parent` should you also be
  employing a proxy cache.

- The client request header X-Forwarded-For may be toggled with
  :ts:cv:`proxy.config.http.insert_squid_x_forwarded_for`.

- The client request header Forwarded may be configured with
  :ts:cv:`proxy.config.http.insert_forwarded`.

Client Configuration
====================

If you are operating your proxy in transparent mode, your clients should require
no special proxy-related configuration.

If you are operating in explicit forward proxy mode, without automatic routing
rules on your network to direct all outbound traffic through the proxy, your
client browsers will need to be directed to the proxy. This may be accomplished
in two different ways.

Clients may be configured to use the default ``8080`` port on your Traffic Server
host as a proxy. This will result in all requests from that client browser being
issued through the single forward proxy as configured.

Security Considerations
=======================

It's important to note that once your Apache Traffic Server is configured as a
forward proxy it will indiscriminately accept proxy requests from anyone. If it
is reachable from the Internet, then you have configured an *Open Proxy*.

This is generally not desirable, as it will permit anyone to potentially use
your network as the source of traffic to sites of their choosing. To avoid
this, you'll have to make sure your proxy server is either only reachable from
within your private network or is secured by firewall rules that permit only
those you wish to have access to the proxy.

