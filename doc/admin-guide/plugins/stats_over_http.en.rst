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

.. _admin-plugins-stats-over-http:

Stats Over HTTP Plugin
**********************

This plugin implements an HTTP interface to all Traffic Server statistics. The
metrics returned are in a JSON format, for easy processing. This plugin is now
part of the standard ATS build process, and should be available after install.

Enabling Stats Over HTTP
========================

To enable this plugin, add to the :file:`plugin.config` file::

    stats_over_http.so

After starting Traffic Server, the JSON metrics are now available on the
default URL::

    http://host:port/_stats

where host and port is the hostname/IP and port number of the server.

Plugin Options
==============

.. option:: --integer-counters

This option causes the plugin to emit floating point and integral
metric values as JSON numbers, rather then JSON strings. This can
cause interoperability problems since integer metrics have a 64-bit
unsigned range.

.. option:: --wrap-counters

This option wraps 64-bit unsigned values to the 64-bit signed range.
This aids interoperability with Java, since prior to the Java SE 8
release, Java did not have a 64-bit unsigned type.

You can optionally modify the path to use, and this is highly
recommended in a public facing server. For example::

    stats_over_http.so 81c075bc0cca1435ea899ba4ad72766b

and the URL would then be e.g.::

    https://host:port/81c075bc0cca1435ea899ba4ad72766b

This is weak security at best, since the secret could possibly leak if you are
careless and send it over clear text.
