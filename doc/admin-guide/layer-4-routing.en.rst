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
.. default-domain:: c
.. _layer-4-routing:

Layer 4 Routing
**************************

|TS| supports a limited set of layer 4 routing options. In such use |TS| acts effectively as a
router, moving network data between two endpoints without modifying the data. The routing is
accomplished by examining the initial data from the inbound connection to decide the outbound
destination. The initial data is then sent to the destination and subsequently |TS| forwards all
data read on one connection to the other and vice versa.

.. image:: ../uml/images/l4-basic-sequence.svg
   :align: center

In this way it acts similary to `nc <https://linux.die.net/man/1/nc>`__.

The primary differences between different types of layer 4 routing is the mechanism by which |TS|
creates the outbound connection. This is described in detail in the type specific documentation.

Transparency
============

Transparency is in some sense layer 4 routing because the outbound connection is determined by
examining the destination address in the client TCP packets. This is discussed in detail
:ref:`elsewhere <transparent-proxy>`.

.. _sni-routing:

SNI Routing
===========

Currently the only directly supported layer 4 routing (as of version 8.0) is SNI based routing. This
imposes some requirements on the traffic.

*  The inbound connection must be TLS.
*  The outbound destination must handle the HTTP ``CONNECT`` method.

SNI routing is configured by :file:`ssl_server_name.yaml`.

If SNI Routing is enabled the initial "`CLIENT HELLO
<https://tools.ietf.org/html/rfc5246#section-7.4.1.2>`__" data of an inbound TLS connection is
examined to extract the "`SNI <https://tools.ietf.org/html/rfc3546#section-3.1>`__" value. This is
matched against the configuration data to select an action for the inbound connection. In this case
the option of interest is ``tunnel_route``. If this is set then |TS| will connect to the specified
destination and issue an HTTP ``CONNECT`` request using the SNI value as the URL for the request.
Because the destination and the ``CONNECT`` are the same in general it will be necessary to use
a plugin to change the URL in the ``CONNECT``.

Example
-------

Consider a Content Delivery Network (CDN) that has an edge layer of externally facing |TS|
instances. The goal is to enable external clients to connect to internal services and do their own
client certificate verification, possibly because distribution of private keys to the edge |TS|
instances is too difficult or too risky. To achieve this, the edge |TS| instances can be configured
to route inbound TLS connections with specific SNI values directly to the internal services without
TLS termination on the edge. This enables the edge to provide controlled external access to the
internal services without each internal service having to stand up its own edge. Note the services
do not require global routable addresses as long as the edge |TS| instances can route to the
services.

The basic set up is therefore

.. figure:: ../uml/images/l4-example-cdn-layout.svg
   :align: center

   A Client connects to an edge |TS| which forwards the connection to the internal Service.
   The Client then negotiates TLS with the Service.


For the example, let us define two services inside the corporate network of Example, Inc.
``service-1`` and ``service-2``. ``service-1`` is on port 443 on host ``app-server-29`` while
``service-2`` is on port 4443 on host ``app-server-56``. The SNI routing set up for this would be

========================== =====================================
SNI value                  Destination
========================== =====================================
service-1.example.com      app-server-29:443
service-2.example.com      app-server-56:4443
========================== =====================================

The :file:`ssl_server_name.yaml` contents would be

.. code-block:: lua

   server_config = {
      {
         fqdn = 'service-1.example.com'
         tunnel_route = 'app-server-29:443'
      },
      {
         fqdn = 'service-2.example.com'
         tunnel_route = 'app-server-56:4443'
      }
   }

The sequence of network activity for a Client connecting to ``service-2`` is

.. image:: ../uml/images/l4-sni-routing-seq.svg
   :align: center

Note the destination for the outbound TCP connection and the HTTP ``CONNECT`` is the same. If this
is a problem (which it will be in general) a plugin is needed to change the URL in the ``CONNECT``.
In this case the proxy request is available in the :c:macro:`TS_HTTP_TXN_START_HOOK` hook. This
cannot be done using remap because for a ``CONNECT`` there is no remap phase. Note that for a
tunneled connection like this, the only transaction hooks that will be triggered are
:c:macro:`TS_HTTP_TXN_START_HOOK` and :c:macro:`TS_HTTP_TXN_CLOSE_HOOK`. In addition, because |TS|
does not terminate (and thefore does not decrypt) the connection, it cannot be cached or served from
cache.
