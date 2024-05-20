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

The following bundles are available in the core today:

============================   ====================================================================
Bundle                         Description
============================   ====================================================================
``Bundle::Common``             For DSCP and an overridable Cache-Control header.
``Bundle::LogsMetrics``        Log sampling, TCPInfo  and per-remap metrics.
============================   ====================================================================

This example shows how a Cript would enable both of these bundles with all features:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   #include <cripts/Bundles/Common.hpp>
   #include <cripts/Bundles/LogsMetrics.hpp>

   do_create_instance()
   {
     Bundle::Common::activate().dscp(0x2e)
                               .cache_control("max-age=3600");

     Bundle::LogsMetrics::activate().logsample(100)
                                    .tcpinfo(true)
                                    .propstats("example.com");
   }

.. note::
   You don't have to activate all components of a Bundle, just leave it out if you don't need it.

.. note::
   The bundles are not enabled by default. You have to explicitly activate them in your Cript,
   with the appropriate include directives. This is because the list of Bundles may grow over time,
   as well as the build system allowing for custom bundles locally.
