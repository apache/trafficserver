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

.. highlight:: cpp
.. default-domain:: cpp

.. _cripts-connections:

Connections
***********

Cripts will manage all client and server connections, in a set
of objects that it manages for the user. This ownership again
implies that access to these objects must be ``borrowed``.

.. code-block:: cpp

   do_remap()
   {
     static cripts::Matcher::Range::IP ALLOW_LIST({"192.168.201.0/24", "10.0.0.0/8"});
     borrow conn = cripts::Client::Connection::Get();
     auto client_ip = conn.IP();

     if (!ALLOW_LIST.contains(client_ip)) {
       // Deny the request (see examples for details)
     }
   }

There are two kinds of connections provided by the run-time system:

================================   =========================================================================
Connection Object                  Description
================================   =========================================================================
``cripts::Client::Connection``     The connection from the client to the ATS server.
``cripts::Server::Connection``     The connection from ATS to parent or origin server.
================================   =========================================================================

As usual, the ``cripts::Server::Connection`` object is only available assuming that the request
is a forward proxy request, and you borrow it with the ``Get()`` method. On cache misses,
there is no such connection.

.. _cripts-connections-methods:

Connection Methods
==================

The connection objects provides a set of methods, used to access some internals details of the
connections. These are:

=======================   =========================================================================
Method                    Description
=======================   =========================================================================
``Count()``               The number of transactions processed on the connection so far.
``IP()``                  The IP address of the connection.
``LocalIP()``             The server (ATS) IP address of the connection.
``IsInternal()``          Returns ``true`` or ``false`` if the connection is internal to ATS.
``Socket()``              Returns the raw socket structure for the connection (use with care).
``IsTLS()``               Returns ``true`` if the connection is a TLS connection.
``ClientCert()``          Returns the client certificiate (mTLS) for the connection (if any).
``ServerCert()``          Returns the server certificate for the connection, if it's a TLS connection.
=======================   =========================================================================

The ``IP()`` and ``LocalIP()`` methods return the IP address as an object. In addition to the
automatic string conversion, it also has a special semantic string conversion which takes
IPv4 and IPv6 CIDR sizes. For example:

.. code-block:: cpp

   do_remap()
   {
     borrow conn = cripts::Client::Connection::Get();
     auto ip = conn.IP();

     CDebug("Client IP CIDR: {}", ip.string(24, 64));

.. _cripts-ip-methods:

IP Object Methods
=================

The IP objects returned by ``IP()`` and ``LocalIP()`` methods provide additional functionality
beyond string conversion:

=======================   =========================================================================
Method                    Description
=======================   =========================================================================
``string()``              Convert IP to string with optional CIDR masking.
``Socket()``              Convert IP to a ``sockaddr`` structure for low-level socket operations.
``Hasher()``              Generate a hash value for the IP address.
``Sample()``              Determine if IP should be sampled based on rate and seed.
``ASN()``                 Get ASN number (if Geo-IP support is available).
``ASNName()``             Get ASN name (if Geo-IP support is available).
``Country()``             Get country name (if Geo-IP support is available).
``CountryCode()``         Get country code (if Geo-IP support is available).
=======================   =========================================================================

.. note::
   The Geo-IP methods (``ASN``, ``ASNName``, ``Country``, ``CountryCode``) are only available if ATS
   has been built with Geo-IP support. These methods can be used on any IP object, not just
   connection IPs.

.. _cripts-connections-variables:

Connection Variables
====================

Both connection objects provide a number of variables that can be accessed. These are:

=======================   =========================================================================
Variable                   Description
=======================   =========================================================================
``tcpinfo``               A number of TCPinfo related fields (see below).
``geo``                   If available (compile time) access to Geo-IP data (see below).
``congestion``            Configure the congestion algorithm used on the socket.
``pacing``                Configure socket pacing for the connection.
``dscp``                  Manage the DSCP value for the connection socket.
``mark``                  Manage the Mark value for the connection socket.
``tls``                   Access to the TLS object for the connection.
=======================   =========================================================================

For other advanced features, a Cript has access to the socket file descriptor, via the ``FD()``
method of the connection object.

.. note::
   For pacing, the special value ``cripts::Pacing::Off`` can be used to disable pacing.

Lets show an example of how one could use these variables:

.. code-block:: cpp

   do_remap()
   {
     borrow conn = cripts::Client::Connection::Get();

     conn.congestion = "bbrv2";
     conn.pacing = 100;
     conn.dscp = 0x2e;
     conn.mark = 0x1;
   }

.. _cripts-connections-tcpinfo-variables:

TCPInfo Variables
=================

The ``tcpinfo`` variable is a structure that provides access to the TCP information for the connection.

=======================   =========================================================================
Field                     Description
=======================   =========================================================================
``rtt``                   The round trip time for the connection.
``rto``                   Retransmission timeout.
``retrans``               The number of retransmissions.
``snd_cwnd``              The congestion window.
``info``                  The *raw* TCP information.
=======================   =========================================================================

In addition to these convenience fields, the ``tcpinfo`` object provides a method to access the raw
TCP information as well in the ``info`` field. There's also a predefined log format, which can be
accessed via the ``Log()`` method. See the ``tcpinfo`` plugin in ATS for details.

.. _cripts-connections-geo-ip:

Geo-IP
======

If ATS has been built with Geo-IP support, both connection objects and IP objects will provide
access to Geo-IP data. There are two ways to access Geo-IP information:

**Connection-based Geo-IP**

The connection object provides access to Geo-IP data for the connection's IP address:

.. code-block:: cpp

   do_send_response()
   {
     borrow conn = cripts::Client::Connection::Get();

     // This is supported, but probably prefer the IP-based approach below
     resp["X-ASN"] = conn.geo.ASN();
     resp["X-Country"] = conn.geo.Country();
   }

**IP-based Geo-IP**

Any IP object can perform Geo-IP lookups directly:

.. code-block:: cpp

   do_send_response()
   {
     borrow conn = cripts::Client::Connection::Get();
     auto client_ip = conn.IP();

     resp["X-ASN"] = client_ip.ASN();
     resp["X-Country"] = client_ip.Country();
   }

The following methods are available for both approaches:

=======================   =========================================================================
Method                    Description
=======================   =========================================================================
``ASN()``                 The ASN number.
``ASNName()``             The name of the ASN.
``Country()``             Country.
``CountryCode()``         Country code.
=======================   =========================================================================

.. note::
   All methods return string values. These are methods and not fields, so they must be called as
   functions. The IP-based approach allows you to perform Geo-IP lookups on any IP address,
   not just connection IPs, making it more flexible for use cases where you have IP addresses
   from other sources.

.. _cripts-connections-tls:

TLS
===

The ``tls`` variable provides access to the TLS object for the session. This object
provides access to the TLS certificate and other TLS related information. The following methods
are available:

=======================   =========================================================================
Method                    Description
=======================   =========================================================================
``Connection``            Returns the connection object for the TLS connection.
``GetX509()``             Returns the X509 certificate for the connection, an OpenSSL object.
=======================   =========================================================================

Both of these can return a null pointer, if the connection is not a TLS connection or
if the certificate is not available. The ``GetX509()`` method can take an optional
boolean argument, which indicates if the certificate should be for a mutual TLS connection. The
default is ``false``, which means that the server certificate for the connection will be returned.
