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

.. _admin-plugins-alt-svc:

Alternative Services Plugin
***********

This plugin lets an administrator provide topologically closer proxy servers based
on the client's IP address. It adds an ``Alt-Svc`` header to the response

Purpose
=======

A globally distributed reverse proxy service is most useful when each client can
access its closest proxy. When a client queries their local DNS resolver, they
may get a suboptimal server because of a mismatch between the local DNS's
IP and the client's IP.

Alternative Services, as defined in `RFC 7838`_, allow any HTTP service to
provide an equivalent service on a different port, protocol, or host using the ``Alt-Svc``
header. In particular, it allows a seamless change in host without the url changing for the
end user. This is supported on the latest versions of Firefox and Chrome for HTTP/2.

.. I don't have it doing the thing where it doesn't include the header if you happen
.. to land on the host that it's recommending. I think I should add that
.. cus then statisticst will be a bit easier.

.. _RFC 7838: https://tools.ietf.org/html/rfc7838

Installation
============

The Alternative Services plugin is currently experimental, and is only available if you compile your project. You will need to add ``--enable-experimental-plugins``
when you ``./configure`` your project. In addition, security considerations require Alternative Services to have Service Name Identification (SNI) and application layer protocol negotiation
(ALPN) as part of the TLS handshake. Therefore, you need to configure with a version of Openssl higher than the default by adding ``--with-openssl=/path/to/openssl/1.0.2k``.
This plugin has been tested and is known to work with Openssl 1.0.2k.

If the plugin has been installed and configured correctly, contacting the server with a mapped IP will result in
an ``Alt-Svc`` response header.

Configuration
=============

The Alternative Services plugin is a global plugin. Add the following to your :file:`plugin.config` file::

    alt_svc.so etc/trafficserver/alt-svc.config

The alt-svc config file looks like this::

    egypt.example.com
     7ee9::/16
     18.0.0.0/8
    morocco.example.com
     7e3a:f3f3::/32
     64.77.0.0/16

A couple notes about this file format:

 - The file must end in a newline character, or the file may not be properly parsed.
 - Host names are listed without a leading space. IP Prefixes are listed with a leading space.
 - All IP Prefixes are in standard CIDR_ format.
 - IPv4 and IPv6 addresses are both supported. IP 6to4 addresses are recognized strictly as IPv6, and do not affect IPv4 routing.
 - The file does not need to be called ``alt-svc.config``. It can also be an absolute path or a path relative to the TrafficServer working directory.

.. _CIDR: https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing

Since Alternative Services requires tls to prevent Man-in-the-Middle attacks, you will need to set up SSL on your
proxy servers (See :ref:`client-and-traffic-server-connections`). In particular, since the browsers we've tested only seem to work with HTTP/2, you should have a line
that looks like this in your :file:`records.config`::

    CONFIG proxy.config.http.server_ports STRING 443:proto=http2:ssl 443:proto=http2:ssl:ipv6

If anything goes wrong in the configuration phase, error messages are logged describing some parsing problems.

SSL Certificate Requirements
=============

Alternative Services requires SNI. In order for SNI to work on all modern browsers, your certificate must include ``subjectAltName`` field.
The following are instructions for creating a self-signed certificate, which may be useful for a trial run with the ``alt-svc`` plugin.

Ensure that you are using the latest version of Openssl (we have tested these with version 1.0.2k). First, create a root CA::

    openssl req -x509 -new -nodes -key rootCA.key -sha256 -days 1024 -out rootCA.pem

You can install this root CA to your browser either through some shared certificate infrastructure (keychain on macOS, pki/nss on Linux),
or in the browser's custom "Install Certificate" security feature.

Next, create a request::

    openssl  req -new -sha256 -nodes -out example.csr -newkey rsa:2048 -keyout example.key -config <( cat example.txt )

The :file:`example.key` is the private key for the TrafficServer instances. The request is encoded into :file:`example.csr` and is used in the next step.
This request is configured with :file:`example.txt`, which looks like this::

    [req]
    default_bits = 2048
    prompt = no
    default_md = sha256
    req_extensions = req_ext
    distinguished_name = dn

    [ dn ]
    C=US
    ST=California
    L=Sunny Valley
    O=Business, Inc.
    OU=Proxy Team
    emailAddress=cache22@business-inc.com
    CN = www.example.com

    [ req_ext ]
    subjectAltName = @alt_names

    [ alt_names ]
    DNS.1 = www.example.com

Finally, this request is signed to create the x509 (public) certificate :file:`example.crt` for the TrafficServer::

    openssl x509 -req -in example.csr -CA rootCA.pem -CAkey rootCA.key -CAcreateserial -out example.crt -days 500 -sha256 -extfile v3.ext

The certificate authority is the same as in the first step. The request :file:`example.csr` was generated in the second step.
The file :file:`v3.ext` looks something like this::

    authorityKeyIdentifier=keyid,issuer
    basicConstraints=CA:FALSE
    keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
    subjectAltName = @alt_names

    [alt_names]
    DNS.1 = www.example.com

In this example, you can replace ``www.example.com`` with any valid fully qualified domain name.
Just make sure it is replaced in the ``CN`` field and both ``alt_names`` ``DNS.1`` fields.

Limitations
===========

 - All services must be configured to serve TLS over HTTP/2 on port 443.
 - This plugin only works for a single semantic service, and is not currently appropriate for usage in a CDN.
 - CIDR should logically work with the most-specific prefix first, but we do not make any such guarantee on nested/intersecting IP subnets.