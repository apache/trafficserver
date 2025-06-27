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

.. _cripts-certs:

Certificates
************

Cripts provides a set of convenient classes for introspection into the various
TLS certificates that are used. These include both the server certificates used
to establish a TLS connections, as well as any client certificates used for
mutual TLS.

In the current implementation, these objects only work on X509 certificates as
associated with the ``client`` and ``server`` connections. Let's start off with
a simple example of how to use these objects:

.. code-block:: cpp

   do_send_response()
   {
     if (client.connection.IsTLS()) {
       const auto tls = cripts::Certs::Server(client.connection);

       client.response["X-Subject"] = tls.subject;
       client.response["X-NotBefore"] = tls.notBefore;
       client.response["X-NotAfter"] = tls.notAfter;
     }
   }

.. _cripts-certs-objects:

Objects
=======

There are two types of objects for the certificates:

=================================   ===============================================================
Object                              Description
=================================   ===============================================================
``cripts::Certs::Server``           The certificate used on the connection for TLS handshakes.
``cripts::Certs::Client``           The mutual TLS (mTLS) certificate used on the connection.
=================================   ===============================================================

This combined with the two kinds of connections, ``cripts::Client::Connection`` and
``cripts::Server::Connection`` yields a total of four possible certificate objects. For example, to
access the client mTLS provided certificate on a client connection, you would use:

.. code-block:: cpp

   const auto tls = cripts::Certs::Client(cripts::Client::Connection::Get());

Or if you are using the convenience wrappers:

.. code-block:: cpp

   const auto tls = cripts::Certs::Client(client.connection);

.. _cripts-certs-x509:

X509 Values
===========

As part of the certificate objects, there are a number of values that can be
accessed. These values are all based on the X509 standard and can be used to
introspect the certificate. The following values are available:

=================================   ===============================================================
Value                               Description
=================================   ===============================================================
``certificate``                     The raw X509 certificate in PEM format.
``signature``                       The raw signature of the certificate.
``subject``                         The subject of the certificate.
``issuer``                          The issuer of the certificate.
``serialNumber``                    The serial number of the certificate.
``notBefore``                       The date and time when the certificate is valid from.
``notAfter``                        The date and time when the certificate is valid until.
``version``                         The version of the certificate.
=================================   ===============================================================

.. _cripts-certs-san:

SAN Values
==========

We've made special provisions to access the Subject Alternative Name (SAN) values
of the certificate. These values are often used to identify the hostnames or IP
addresses that the certificate is valid for. Once you have the certificate object,
you can access the SAN values as follows:

====================   ===============   ===============================================================
Field                  X509 field        Description
====================   ===============   ===============================================================
``.san``               na                An array of tuples with type and ``string_view`` of  all SANs.
``.san.email``         ``GEN_EMAIL``     An array of ``string_view`` of email addresses.
``.san.dns``           ``GEN_DNS``       An array of ``string_view`` of DNS names.
``.san.uri``           ``GEN_URI``       An array of ``string_view`` of URIs.
``.san.ipadd``         ``GEN_IPADD``     An array of ``string_view`` of IP addresses.
====================   ===============   ===============================================================

.. note::

   These arrays are empty if no SAN values are present in the certificate. We also populate these
   arrays lazily, but they are kept for the lifetime of the certificate object. This means that
   you can access these values multiple times without incurring additional overhead. Remember
   that you can use the ``cripts::Net::IP`` class to convert the IP addresses into proper
   IP address objects if needed.


Odds are that you will want to use one of the specific array values, such as ``.san.uri``, which is
easily done in a simple loop:

.. code-block:: cpp

   do_remap()
   {
     if (client.connection.IsTLS()) {
       const auto tls = cripts::Certs::Server(client.connection);

       for (auto uri : tls.san.uri) {
         // Check the URI string_view
       }
     }
   }


You can of course loop over all SAN values, which is where the type of the value would come in handy,
and why this is an array of tuples. In this scenario, you would iterate over the tuples like this:

.. code-block:: cpp

   do_remap()
   {
     if (client.connection.IsTLS()) {
       const auto tls = cripts::Certs::Server(client.connection);

       for (const [type, san] : tls.san) {
         if (type == cripts::Certs::SAN::URI) {
           // Check the URI string here
         } else if (type == cripts::Certs::SAN::DNS) {
           // Check the DNS string here
         }
       }
     }
   }

In addition to traditional C++ iterators, you can also access SAN values by index. Make sure
you check the size of the array first, as accessing an out-of-bounds index will give you an
empty tuple. Prefer the iterator above, unless you know you want to access a specific element.

Example of an alternative way to loop over all SAN values:

.. code-block:: cpp

   do_remap()
   {
     if (client.connection.IsTLS()) {
       const auto tls = cripts::Certs::Server(client.connection);

       size_t san_count = tls.san.size();

       for (size_t i = 0; i < san_count; ++i) {
         const auto [type, san] = tls.san[i];
         // Process the type and san as needed
       }
     }
   }
