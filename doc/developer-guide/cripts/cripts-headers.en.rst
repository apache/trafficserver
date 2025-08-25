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

.. _cripts-headers:

Headers
*******

Just like the URL objects, all header objects are managed and owned by the Cripts
system, and must always be borrowed. The pattern for this is as follows:

.. code-block:: cpp

  borrow req = cripts::Client::Request::Get();

  auto foo = req["X-Foo"];

There are four types of headers that can be borrowed:

=============================   ===========================================================================
Header Object                   Description
=============================   ===========================================================================
``cripts::Client::Request``     The client request headers. Always available.
``cripts::Client::Response``    The client response headers.
``cripts::Server::Request``     The server request headers.
``cripts::Server::Response``    The server response headers.
``cripts::Cache::Response``     The cached response headers (immutable and conditional).
=============================   ===========================================================================

.. note::

   For all of these headers, except the ``cripts::Client::Request``, the headers are not
   available until the respective hook is called. For example, the ``cripts::Client::Response`` headers
   are not available until the response headers are received from the origin server or cache lookup.
   The ``cripts::Cache::Response`` header is also immutable, and can only be used after a successful
   cache lookup.

Assigning the empty value (``""``) to a header will remove it from the header list. For example:

.. code-block:: cpp

  borrow req = cripts::Client::Request::Get();

  req["X-Foo"] = "bar"; // Set the header
  req["X-Fie"] = "";    // Remove the header

A header can also be removed by using the ``Erase`` method, which is a little more explicit:

.. code-block:: cpp

  borrow req = cripts::Client::Request::Get();

  req.Erase("X-Foo");

.. note:: There is also a Cripts Bundle for headers, see :ref:`Bundles <cripts-bundles-headers>`.

.. _cripts-headers-iterators:

Iterators
---------

A common use pattern with the header values is to iterate (loop) over all of them. This is easily done with
a pattern such as the following example:

.. code-block:: cpp

  borrow req = cripts::Client::Request::Get();

  for (auto header : req) {
    CDebug("Header: {}: {}", header, req[header]); // This will print all headers and their values
  }

The request object implements a standard C++ style iterator, so any type of loop that works with
standard C++ iterators will work with the request object.

.. _cripts-headers-methods:

Methods
-------

In addition to holding all the request headers, the request objects also holds the ``method`` verb.
For the ``cripts::Client::Request`` object, this is the client request method (GET, POST, etc.). For the
``cripts::Server::Request`` object, this is the method that will be sent to the origin server (GET, POST, etc.).

Cripts provides the following convenience symbols for common methods (and performance):

============================   ======================================================================
Method                         Description
============================   ======================================================================
``cripts::Method::GET``        The GET method.
``cripts::Method::POST``       The POST method.
``cripts::Method::PUT``        The PUT method.
``cripts::Method::DELETE``     The DELETE method.
``cripts::Method::HEAD``       The HEAD method.
``cripts::Method::OPTIONS``    The OPTIONS method.
``cripts::Method::CONNECT``    The CONNECT method.
``cripts::Method::TRACE``      The TRACE method.
============================   ======================================================================

These symbols can be used to compare against the method in the request object. For example:

.. code-block:: cpp

  borrow req = cripts::Client::Request::Get();

  if (req.method == cripts::Method::GET) {
      // Do something
  }

.. _cripts-headers-status:

Status
------

The two response objects also hold the status code of the response. This includes:

==========================   ======================================================================
Member                       Description
==========================   ======================================================================
``status``                   The status code of the response. E.g. ``200``.
``reason``                   The reason phrase of the response. E.g. ``OK``.
==========================   ======================================================================


Lookup Status
-------------

The ``Cache::Response`` header also hold the status of the cache lookup, in a member named
``cachelookup``. This is an integer value that represents the status of the cache lookup.
The possible values are:

===============================   ======================================================================
Value                             Description
===============================   ======================================================================
``LookupStatus::NONE``     (-1)   No lookup status available.
``LookupStatus::MISS``      (0)   The response was not found in the cache.
``LookupStatus::HIT_STALE`` (1)   The response was found in the cache, but it was stale.
``LookupStatus::HIT_FRESH`` (2)   The response was found in the cache and is fresh.
``LookupStatus::SKIPPED``   (3)   We skipped cache lookup completely
===============================   ======================================================================

A string representation of the cache lookup status is also available in in the ``lookupstatus`` member,
via an implicit conversion to a string


==========================   ======================================================================
Value                        Description
==========================   ======================================================================
``none``                     No lookup status available.
``miss``                     The response was not found in the cache.
``hit-stale``                The response was found in the cache, but it was stale.
``hit-fresh``                The response was found in the cache and is fresh.
``skipped``                  We skipped cache lookup completely
==========================   ======================================================================

This status can be used to determine if the response was found in the cache, and if so, if it was
fresh or stale. Example usage of the cache lookup status:

.. code-block:: cpp

  do_read_response() {
    borrow cached = cripts::Cache::Response::Get();

    if (cached.lookupstatus == cripts::LookupStatus::MISS) {
      // Do something
    }
  }
