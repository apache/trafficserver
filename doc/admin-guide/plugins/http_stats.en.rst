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

.. _admin-plugins-http-stsatus:

HTTP Stats Plugin
**********************

This plugin implements an HTTP interface to all Traffic Server statistics. The
metrics returned are in a JSON format by default. You can also configure the output
to be CSV as well.

Since this is a remap plugin, you can chain other plugins to affect the output (eg
compressing the output via the :doc:`compress.en`.

Enabling HTTP Stats
========================

To use this plugin, add a mapping to the :file:`remap.config` file::

    map /_stats http://localhost @plugin=http_stats.so

After starting Traffic Server, the JSON metrics are now available at the URL::

    http://host:port/_stats

where host and port is the hostname/IP and port number of |TS|.

This will expose the stats to anyone who could access the |TS| instance. It's recommended you
use one of the ACL features in |TS|. For example::

    map /_stats \
        http://127.0.0.1 \
        @plugin=http_stats.so \
        @src_ip=127.0.0.1 @src_ip=::1 \
        @src_ip=10.0.0.0-10.255.255.255 \
        @action=allow

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

.. option:: --csv

By default, `http_stats.so` will output all the stats in JSON format. Specify this
option on the remap command-line to enable this.

.. option:: --max-age=<int>

If set, this will result in a ``Last-Modified`` header and a ``Cache-Control`` header of ``max-age=<int>``
