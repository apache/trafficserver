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
metrics returned are in a JSON format by default, for easy processing. You can
also output the stats in CSV format as well. This plugin is now part of the
standard ATS build process, and should be available after install.

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

Config File Usage
=================

stats_over_http.so also accepts a configuration file taken as a parameter

The plugin first checks if the parameter that was passed in is a file that exists, if so
it uses that as a config file, otherwise if a parameter exists it assumes that it is meant
to be used a path value (as if you were not using a config file)

You can add comments to the config file, starting with a `#` value

Other options you can specify:

.. option:: path=

This sets the path value for stats

.. option:: allow_ip=

A comma separated list of IPv4 addresses allowed to access the endpoint

.. option:: allow_ip6=

A comma separated list of IPv6 addresses allowed to access the endpoint

Output Format
=============

By default stats_over_http.so will output all the stats in JSON format. However
if you wish to have it in CSV format you can do so by passing an ``Accept`` header:

.. option:: Accept: text/csv

In either case the ``Content-Type`` header returned by stats_over_http.so will reflect
the content that has been returned, either ``text/json`` or ``text/csv``.
