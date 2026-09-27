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

To enable this plugin as a global plugin, add it to the :file:`plugin.config` file::

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
cause interoperability problems since counter metrics have a 64-bit
unsigned range.

.. option:: --wrap-counters

This option wraps 64-bit unsigned counter values to the 64-bit signed range.
This aids interoperability with Java, since prior to the Java SE 8
release, Java did not have a 64-bit unsigned type. Gauge values are
signed, so this option does not change them.

.. option:: --no-prometheus-help

This option omits the ``# HELP`` lines from the Prometheus output. Each
``# HELP`` line repeats the name of a metric, so the option makes the
Prometheus output about half as large.

.. option:: --max-age-ms=N

The plugin reuses a render for later requests in the same format and
encoding, for ``N`` milliseconds after the render starts. With ``0``, each
request waits for a render that starts after the request arrives. The default
is ``0`` for the global plugin and ``1000`` for a remap rule. ``N`` must be
from ``0`` to ``86400000``, one day. See `Rendering`_.

.. option:: --wait-timeout-ms=N

When requests wait and no render finishes for ``N`` milliseconds, the
requests get a ``503 Service Unavailable`` response. The default is ``10000``.
``N`` must be from ``1`` to ``86400000``, one day. See `Rendering`_.

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

Prometheus formatted output is also supported via the ``Accept`` header. Version 0.0.4
(flat metric names) and version 2.0.0 (labeled metrics for better aggregation)
are supported:

.. option:: Accept: text/plain; version=0.0.4
.. option:: Accept: text/plain; version=2.0.0

Alternatively, the output format can be specified as a suffix to the configured
path in the HTTP request target.  The supported suffixes are ``/json``,
``/csv``, ``/prometheus``, and ``/prometheus_v2``.  For example, if the path
is set to ``/_stats`` (the default), you can access the stats in CSV format by
using the URL::

    http://host:port/_stats/csv

The Prometheus version 0.0.4 format (flat) can be requested by using the URL::

    http://host:port/_stats/prometheus

The Prometheus v2 labeled format can be requested by using the URL::

    http://host:port/_stats/prometheus_v2

The JSON format is the default, but you can also access it explicitly by using the URL::

    http://host:port/_stats/json

In both Prometheus formats, each metric family appears once: its ``# HELP``
and ``# TYPE`` lines, then all of its samples. The first metric of a family
sets the type of the family, and the name of that metric is the ``# HELP``
text. In the version 0.0.4 format, a family whose first metric is a float has
no ``# TYPE`` line. The plugin translates the name of each metric once and
reuses the result for later requests. A metric that Traffic Server creates
after the first request joins its family in later responses.

Note that using a path suffix overrides any ``Accept`` header. Thus if you
specify a path suffix, the plugin will return the data in that format regardless of
the ``Accept`` header.

In either case the ``Content-Type`` header returned by ``stats_over_http.so`` will
reflect the content that has been returned: ``text/json``, ``text/csv``,
``text/plain; version=0.0.4; charset=utf-8``, or
``text/plain; version=2.0.0; charset=utf-8`` for JSON, CSV, Prometheus v1, and
Prometheus v2 formats respectively.


Stats over http also accepts returning data in gzip or br compressed format per the
``Accept-encoding`` header. If the header is present, the plugin will return the
data in the specified encoding, for example:

.. option:: Accept-encoding: gzip, br

The plugin compresses gzip and deflate responses at zlib level 6, and br responses
at brotli quality 6 with a 64 KiB window.

Rendering
=========

The plugin renders the statistics on a task thread, so that a render of many
metrics does not hold up an event thread. The request waits for the render,
and then Traffic Server sends the response from an event thread.

At most one render at a time runs for the global plugin, and at most one for
each remap rule. One render answers every request that waits for it, in each format
and encoding that they ask for. A render answers later requests for the same
format and encoding for the time that :option:`--max-age-ms` sets, so a request
can get values that are up to that old. After that time, the next request
starts a new render.

If no render finishes within the time that :option:`--wait-timeout-ms` sets,
the waiting requests get a ``503 Service Unavailable`` response with an empty
body and no ``Content-Type``. A remap rule adds its ``X-Stats-Format`` header to
this response. The time starts when a request starts to wait and no other
request is waiting. A request that starts to wait later does not restart it, so
that request can get the ``503`` before it has waited the full time. When a
render finishes and requests still wait for another render, the time starts
again.

Remap Plugin Usage
==================

The plugin can also serve the statistics from a rule in :file:`remap.config`.
In this mode the plugin needs no :file:`plugin.config` entry and adds no global
hook. The ACL filters of the rule control access, and each rule serves one
format. For example::

    map http://example.com/metrics http://127.0.0.1/ \
        @plugin=stats_over_http.so @pparam=--format=prometheus \
        @action=deny @src_ip=~10.0.0.0/8

The filter ``@action=deny @src_ip=~10.0.0.0/8`` denies every client outside
``10.0.0.0/8``. See :ref:`acl-filters`. The plugin answers the request itself, so
Traffic Server does not connect to the target of the rule. Traffic Server still
resolves the host name of the target, so use an IP address, for example
``127.0.0.1``, as the target.

A rule accepts these options:

``--format=json|csv|prometheus|prometheus_v2``
   The format of every response from the rule. The default is ``json``. The path
   and the ``Accept`` header of the request do not change the format.

``--integer-counters``, ``--wrap-counters``, ``--no-prometheus-help``, ``--max-age-ms`` and ``--wait-timeout-ms``
   These options have the same effect as for the global plugin, for this rule
   only.

The rule answers ``GET`` with the statistics. It answers ``HEAD`` with the same
headers, but without ``Content-Length`` and without a body. It answers other
methods with ``405 Method Not Allowed`` and ``Allow: GET, HEAD``.

A ``200`` response to ``GET``, and a response to ``HEAD``, have these headers:

* ``Content-Type`` as for the global plugin, for example
  ``text/plain; version=0.0.4; charset=utf-8`` for ``prometheus``.
* ``Content-Length``, for ``GET`` only.
* ``Cache-Control: no-store``. Traffic Server does not cache the response.
* ``X-Stats-Format``, with the value of ``--format``, for example
  ``X-Stats-Format: prometheus``. A client can use this header to tell a
  statistics response from the response of another rule, such as a catch-all rule.

A remap rule does not compress its responses.
