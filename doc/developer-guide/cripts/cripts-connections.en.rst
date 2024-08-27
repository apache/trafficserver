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

If ATS has been built with Geo-IP support, the connection object will provide access to the Geo-IP
data for the connection. The following methods will then be available:

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
   functions.
