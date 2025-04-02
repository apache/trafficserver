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

.. _cripts-convenience:

Convenience APIs
****************

To make Cripts even more approachable, a set of optional convenience APIs are added
to the core and top level name space. These APIs are not required to be used, and
are enabled by adding a define to your Cript file (or when compiling the Cript).
Making this addition optional allows users to choose whether they want to use
these convenience APIs or stick with the traditional Cripts API style.

The convenience APIs carries a small (very small) overhead, due to how Cripts defers
(or delays) initializations of objects until they are actually used. These new APIs
are designed to be as efficient as possible, but they do introduce potential conflicts
in the top level namespace. Here's a simple example of how to enable these APIs:

.. code-block:: cpp

   #define CRIPTS_CONVENIENCE_APIS 1

   #include <cripts/Preamble.hpp>

   do_remap()
   {
     urls.request.query.Keep({"foo", "bar"});
   }

   #include <cripts/Epilogue.hpp>


.. _cripts-convenience-toplevel:

Top level API additions
=======================

For the most common patterns, the top level API additions are ``client``, ``server``, and
``urls``. These all have sub-level API additions, as explained in the following table:

===========================   =============================================
Object                        Traditional API equivalent
===========================   =============================================
``client.request``            ``borrow cripts::Client::Request::Get()``
``client.response``           ``borrow cripts::Client::Response::Get()``
``client.connection``         ``borrow cripts::Client::Connection::Get()``
``server.request``            ``borrow cripts::Server::Request::Get()``
``server.response``           ``borrow cripts::Server::Response::Get()``
``server.connection``         ``borrow cripts::Server::Connection::Get()``
``urls.request``              ``borrow cripts::Client::URL::Get()``
``urls.pristine``             ``borrow cripts::Pristine::URL::Get()``
``urls.cache``                ``borrow cripts::Cache::URL::Get()``
``urls.parent``               ``borrow cripts::Parent::URL::Get()``
``urls.remap.to``             ``borrow cripts::Remap::To::URL::Get``
``urls.remap.from``           ``borrow cripts::Remap::From::URL::Get``
===========================   =============================================

The use of these top-level objects are identical to how you would use them with the traditinoal
APIs. The following code shows both the traditional Cripts API and the new APIs in a simple
example:

.. code-block:: cpp

   // Traditional Cripts API
   do_remap()
   {
     borrow req = cripts::Client::Request::Get();
     borrow url = cripts::Client::URL::Get();

     url.query.Keep({"foo", "bar"});
     req["X-Foo"] = "bar"
   }

   // Convenience API, does not need the borrow statements
   do_remap()
   {
     urls.request.query.Keep({"foo", "bar"});
     client.request["X-Foo"] = "bar";
   }

.. note::
   The name for the classic client URL will change to ``cripts::Request::URL`` in
   a future version of ATS, to be inline with these new convenience APIs.

.. _cripts-convenience-macros:

Convenience macros
==================

In addition to the top-level APIs, a set of convenience macros are provided as well,
enabled with the same ``#define`` as above. The following macros have been added,
which again populates in the top level namespace:

===========================   ====================================================================
Macro                         Traditional API equivalent
===========================   ====================================================================
``Regex(name, ...)``          ``static cripts::Matcher::PCRE name(...)``
``ACL(name, ...)``            ``static cripts::Matcher::Range::IP name(...)``
``CreateCounter(id, name)``   ``instance.metrics[id] = cripts::Metrics::Counter::Create(name)``
``CreateGauge(id, name)``     ``instance.metrics[id] = cripts::Metrics::Gauge::Create(name)``
``FilePath(name, path)``      ``static const cripts::File::Path name(path)``
``UniqueUUID()``              ``cripts::UUID::Unique::Get()``
``TimeNow()``                 ``cripts::Time::Local::Now()``
===========================   ====================================================================

An example of using ACLs and regular expressions:

.. code-block:: cpp

   do_remap()
   {
     Regex(rex, "^/([^/]+)/(.*)$");
     ACL(allow, {"192.168.201.0/24", "10.0.0.0/8"});

     if (allow.Match(client.connection.IP()) && rex.Match(urls.request.path)) {
       // do something
     }
   }
