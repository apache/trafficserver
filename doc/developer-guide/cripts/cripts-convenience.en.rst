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

The convenience APIs carry a small (very small) overhead, due to how Cripts defers
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

.. note::
   The convenience APIs must be enabled before including the Preamble header.
   This define affects the entire compilation unit, so use it consistently
   throughout your Cript file.

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
``client.url``                ``borrow cripts::Client::URL::Get()``
``server.request``            ``borrow cripts::Server::Request::Get()``
``server.response``           ``borrow cripts::Server::Response::Get()``
``server.connection``         ``borrow cripts::Server::Connection::Get()``
``urls.request``              ``borrow cripts::Client::URL::Get()``
``urls.pristine``             ``borrow cripts::Pristine::URL::Get()``
``urls.cache``                ``borrow cripts::Cache::URL::Get()``
``urls.parent``               ``borrow cripts::Parent::URL::Get()``
``urls.remap.to``             ``borrow cripts::Remap::To::URL::Get()``
``urls.remap.from``           ``borrow cripts::Remap::From::URL::Get()``
===========================   =============================================

The use of these top-level objects are identical to how you would use them with the traditional
APIs. The following code shows both the traditional Cripts API and the new APIs in a simple
example:

.. code-block:: cpp

   // Traditional Cripts API
   do_remap()
   {
     borrow req = cripts::Client::Request::Get();
     borrow url = cripts::Client::URL::Get();

     url.query.Keep({"foo", "bar"});
     req["X-Foo"] = "bar";
   }

   // Convenience API, does not need the borrow statements
   do_remap()
   {
     urls.request.query.Keep({"foo", "bar"});
     client.request["X-Foo"] = "bar";
   }

.. note::
   Both ``client.url`` and ``urls.request`` refer to the same underlying object, which is
   ``cripts::Client::URL``. This means that any changes made to ``urls.request``
   will also be reflected in ``client.url`` and vice versa.

.. _cripts-convenience-macros:

Convenience macros
==================

In addition to the top-level APIs, a set of convenience macros are provided as well,
enabled with the same ``#define`` as above. The following macros have been added,
which again populate the top level namespace:

===========================   ====================================================================
Macro                         Traditional API equivalent
===========================   ====================================================================
``Regex(name, ...)``          ``static cripts::Matcher::PCRE name(...)``
``ACL(name, ...)``            ``static cripts::Matcher::Range::IP name(...)``
``StatusCode(code, ...)``     ``cripts::Error::Status::Set(code, ...)``
``CreateCounter(id, name)``   ``instance.metrics[id] = cripts::Metrics::Counter::Create(name)``
``CreateGauge(id, name)``     ``instance.metrics[id] = cripts::Metrics::Gauge::Create(name)``
``FilePath(name, path)``      ``static const cripts::File::Path name(path)``
``UniqueUUID()``              ``cripts::UUID::Unique::Get()``
``TimeNow()``                 ``cripts::Time::Local::Now()``
===========================   ====================================================================

These macros provide a more concise syntax for common operations. Here are some examples:

.. _cripts-convenience-macros-examples:

Regex and ACL Example
---------------------

.. code-block:: cpp

   #define CRIPTS_CONVENIENCE_APIS 1
   #include <cripts/Preamble.hpp>

   do_remap()
   {
     Regex(path_regex, "^/api/v([0-9]+)/(.*)$");
     ACL(internal_networks, {"192.168.0.0/16", "10.0.0.0/8", "172.16.0.0/12"});

     if (internal_networks.Match(client.connection.IP())) {
       if (path_regex.Match(urls.request.path)) {
         // Internal API access allowed
         client.request["X-API-Version"] = path_regex[1]; // First capture group
         client.request["X-API-Path"] = path_regex[2];    // Second capture group
       }
     } else {
       StatusCode(403, "Access denied");
     }
   }

   #include <cripts/Epilogue.hpp>

Metrics and File Example
------------------------

.. code-block:: cpp

   #define CRIPTS_CONVENIENCE_APIS 1
   #include <cripts/Preamble.hpp>

   do_create_instance()
   {
     CreateCounter(0, "requests.total");
     CreateGauge(1, "active.connections");
     FilePath(config_file, "/etc/ats/custom.conf");
   }

   do_remap()
   {
     instance.metrics[0]->Increment(); // Increment request counter

     auto uuid = UniqueUUID();
     auto timestamp = TimeNow();

     client.request["X-Request-ID"] = uuid;
     client.request["X-Timestamp"] = timestamp.Epoch();
   }

   #include <cripts/Epilogue.hpp>

.. _cripts-convenience-performance:

Performance Considerations
==========================

The convenience APIs are designed to have minimal overhead:

1. **Lazy Initialization**: Objects are only initialized when first accessed
2. **Reference Semantics**: The convenience objects are references to the same underlying objects
3. **Compile-time Macros**: Most convenience macros expand to the same code as traditional APIs

However, there are some considerations:

- **Namespace Pollution**: The convenience APIs add names to the global namespace
- **Debugging**: Stack traces may show convenience wrapper functions
- **Compatibility**: Code using convenience APIs requires the ``#define`` to compile
- **Performance**: While the overhead is minimal, it may not be suitable for performance-critical
  code due to the lazy initialization.

.. _cripts-convenience-best-practices:

Best Practices
==============

When using convenience APIs:

1. **Consistent Usage**: Either use convenience APIs throughout a Cript or stick to traditional APIs
2. **Documentation**: Comment when using convenience APIs for team clarity.
3. **Testing**: Test both with and without convenience APIs if maintaining compatibility.

Example of mixed usage (not recommended):

.. code-block:: cpp

   do_remap()
   {
     // Mixed usage - avoid this pattern!
     borrow req = cripts::Client::Request::Get(); // Traditional
     urls.request.query.Keep({"foo"});            // Convenience
   }

Better approach:

.. code-block:: cpp

   do_remap()
   {
     // Consistent convenience API usage
     client.request["X-Processed"] = "true";
     urls.request.query.Keep({"foo", "bar"});
   }

.. note::
   The ``StatusCode`` macro currently only works with the status code, the reason
   message can not be set or overridden. Fixing this will require a future change
   in the Traffic Server code base.
