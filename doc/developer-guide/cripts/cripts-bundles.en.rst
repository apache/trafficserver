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

.. _cripts-bundles:

Bundles
*******

While developing Cripts, we realized that Cripts often repeat the same,
common patterns of code. To minimize such duplications across 100's or
even 1000's of scripts, we introduced the concept of a bundle.

A bundle is a collection of functions, classes, and other definitions
turning these common patterns into easily reusable components. A bundle
must be activated in the ``do_create_instance()`` hook of a Cript. This
does *not* exclude doing additional hooks in the Cript itself.

.. note::
   The member variables of a bundle are always lower case, and not
   Pascal case like methods. This is because even though they technically
   are functions, they act more like variables with a value.

The following bundles are available in the core today:

============================   ====================================================================
Bundle                         Description
============================   ====================================================================
``Bundle::Common``             For DSCP and an overridable Cache-Control header.
``Bundle::LogsMetrics``        Log sampling, TCPInfo  and per-remap metrics.
``Bundle::Headers``            For removing or adding headers.
``Bundle::Caching``            Various cache controlling behavior.
============================   ====================================================================

This example shows how a Cript would enable both of these bundles with all features:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   #include <cripts/Bundles/Common.hpp>
   #include <cripts/Bundles/LogsMetrics.hpp>

   do_create_instance()
   {
     Bundle::Common::Activate().dscp(10)
                               .via_header("client", "basic")
                               .set_config({{"proxy.config.srv_enabled", 0},
                                            {"proxy.config.http.response_server_str", "ATS"});

     Bundle::LogsMetrics::Activate().logsample(100)
                                    .tcpinfo(true)
                                    .propstats("example.com");

     Bundle::Caching::Activate().cache_control("max-age=259200")
                                .disable(true)

   }

The ``set_config()`` function can also take a single configuration and value, without the need
to make a list.

.. note::
   You don't have to activate all components of a Bundle, just leave it out if you don't need it.

.. note::
   The bundles are not enabled by default. You have to explicitly activate them in your Cript,
   with the appropriate include directives. This is because the list of Bundles may grow over time,
   as well as the build system allowing for custom bundles locally.

.. _cripts-bundles-via-header:

Via Header
==========

The ``Bundle::Common`` bundle has a function called ``via_header()`` that adds a Via header to the
client response or the origin request. The first argument is ``client`` or ``origin``, and the second
argument is the type of Via header to be used:

============================   ====================================================================
Type                           Description
============================   ====================================================================
``disable``                    No Via header added.
``protocol``                   Add the basic protocol and proxy identifier.
``basic``                      Add basic transaction codes.
``detailed``                   Add detailed transaction codes.
``full``                       Add full user agent connection protocol tags.
============================   ====================================================================

.. _cripts-bundles-headers:

Headers
=======

Even though adding or removing headers in Cripts is very straight forward, we've added the ``Bundle::Headers``
for not only convenience, but also for easier integration and migratino with existing configurations. There
are two main functions in this bundle:

- ``rm_headers()``: Add a header to the request or response.
- ``add_headers()``: Remove a header from the request or response.

The ``rm_headers()`` function takes a list of headers to remove, while the ``add_headers()`` function takes a list
of key-value pairs to add. The header values here are strings, but they can also be strings with the special
operators from the ``header_rewrite`` plugin. For example:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>
   #include <cripts/Bundles/Headers.hpp>

   do_create_instance()
   {
     Bundle::Headers::Activate().rm_headers({"X-Header1", "X-Header2"})
                                .add_headers({{"X-Header3", "value3"},
                                              {"X-Header4", "%{FROM-URL:PATH}"},
                                              {"X-Header5", "%{ID:UNIQUE}"} });
   }
