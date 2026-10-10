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
text. In the version 0.0.4 format without `Prometheus Rules`_, a family whose
first metric is a float has no ``# TYPE`` line. The plugin translates the name
of each metric once and reuses the result for later requests. A metric that
Traffic Server creates after the first request joins its family in later
responses.

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
   and the ``Accept`` header of the request do not change the format. The
   Prometheus output of a rule has no ``current_time_epoch_ms`` sample.

``--integer-counters``, ``--wrap-counters``, ``--no-prometheus-help``, ``--max-age-ms`` and ``--wait-timeout-ms``
   These options have the same effect as for the global plugin, for this rule
   only.

``--config=FILE``
   Read more settings for the rule from ``FILE``, a YAML file. A relative path is
   relative to the configuration directory of Traffic Server. See
   `Configuration File`_.

``--on-config-error=fail|503``
   What to do when ``FILE`` is missing or has an error:

   * ``fail``, the default: the rule fails to load, and so does the whole
     remap configuration. At startup, Traffic Server exits. On a reload,
     Traffic Server keeps the old remap configuration.
   * ``503``: the plugin logs the error. The rule answers each ``GET`` and
     ``HEAD`` request with ``503 Service Unavailable``, an ``X-Stats-Format``
     header and no body.

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

Configuration File
==================

The ``--config`` file can set the format and the other settings of a remap
rule. Its ``prometheus`` section sets how the plugin names the metrics in the
``prometheus`` format. Each key is optional::

    format: prometheus
    render:
      max_age_ms: 1000
      wait_timeout_ms: 10000
    prometheus:
      exclude:
        names: [proxy.process.http.tunnels]
        match: ['^proxy\.process\.update\.']
      rules:
        - match: '^(proxy\.process\.http)\.([0-9xX]{3})_(responses)$'
          labels: {code: 2}
      name:
        replace:
          - {from: '+', to: 'plus'}
        invalid: '[^a-zA-Z0-9_]'
      types:
        source: rules
        rules:
          - {match: 'current', type: gauge}
          - {match: '', type: counter}   # '' matches every name
      strings:
        label: value
        names: [proxy.process.version.server.short]
      const_labels: {region: west}
      limits:
        max_series: 0
      help: false

``format``, ``render.max_age_ms`` and ``render.wait_timeout_ms`` have the
effect of ``--format``, :option:`--max-age-ms` and :option:`--wait-timeout-ms`.
An option on the remap rule takes precedence over the same setting in the file.
The plugin rejects a key that it does not know.

The ``prometheus`` section works only with the ``prometheus`` format. With
another format, the plugin treats this as an error in the file, and
``--on-config-error`` applies.

Traffic Server tracks the file as a part of the remap configuration. After you
change the file, run ``traffic_ctl config reload``. Traffic Server then loads
the remap configuration again, from :file:`remap.yaml` or :file:`remap.config`,
and with it the file. You do not need to change the remap configuration.

Prometheus Rules
----------------

The ``prometheus`` settings turn each metric into a sample of a family, with
labels. The plugin applies them once to each metric name. Each regular expression
is a PCRE2 search for a match anywhere in the name.

``exclude.names`` and ``exclude.match``
   The output leaves out the metrics with these exact names, and the metrics
   whose names match one of these regular expressions.

``rules``
   The plugin tries the rules in file order and uses the first rule whose
   ``match`` matches the name. A name that no rule matches is the family name,
   without labels.

   * ``match`` needs at least one capture group.
   * ``labels`` maps each label name to a capture group, from ``1`` to the
     number of groups. The value of a label is the text of its group.
   * The family name is capture group ``1``, then each later group that is not
     a label and not empty, each with ``_`` before it. When group ``1`` is a
     label, the family name starts with ``_``.

   For example, the file above turns ``proxy.process.http.200_responses`` into
   the sample ``proxy_process_http_responses{code="200",region="west"}``.

``name.replace`` and ``name.invalid``
   The plugin replaces each ``from`` with its ``to`` in the family name, in order.
   Then each character of the family name that ``name.invalid`` matches becomes
   ``_``. The default of ``name.invalid`` is ``[^a-zA-Z0-9_:]``. Label values do
   not change. The output leaves out a metric whose family name is not a valid
   Prometheus metric name, for example a name that starts with a digit.

``types.source`` and ``types.rules``
   With ``record``, the default, a counter metric is a ``counter``, and other
   metrics are a ``gauge``. With ``rules``, the first rule in ``types.rules``
   whose ``match`` matches the name sets the type, ``counter``, ``gauge`` or
   ``untyped``. The rules match the name after the plugin removes the first
   occurrence of each label value from it, so a label value cannot change the type
   of a family. A name that no type rule matches gets the type that ``record``
   gives it.

``strings.names`` and ``strings.label``
   A string metric in ``strings.names`` is a sample with the value ``1``, and
   its string in the label ``strings.label``, ``value`` by default. For
   example, ``proxy_process_version_server_short{value="10.2.0"} 1``. The output
   leaves out a string metric that is not in ``strings.names``, unless its
   string is a decimal number, ``Inf``, ``Infinity`` or ``NaN``. In that case, the
   number is the value of its sample. No constant label or label of a rule
   can have the same name as the label ``strings.label``.

``const_labels``
   Labels that every sample has.

``limits.max_series``
   The maximum number of samples in a response. When there are more, the output
   leaves out the newest metrics, so that each response has the same series.
   ``0``, the default, means no limit.

``help``
   Whether each family has a ``# HELP`` line. The default is ``false``, so a
   remap rule with a ``prometheus`` section writes no ``# HELP`` lines unless
   ``help`` is ``true``. A remap rule without a ``prometheus`` section writes
   them, as the global plugin does. :option:`--no-prometheus-help` on the remap
   rule omits the lines, whatever ``help`` is.

The plugin writes each family once: its ``# TYPE`` line, then all of its
samples. A family of type ``untyped`` has no ``# TYPE`` line. The plugin sorts
the families by name, the samples of each family by their labels other than
``strings.label``, and the labels of each sample by name. Within a family:

* The first metric of the family that the plugin sees sets the type of the
  family, even when the output leaves out that metric. A metric with another
  type gets the type of the family.
* The label names of the family come from the metric whose rule comes last in
  the file, even when that metric has left the statistics, until Traffic Server
  loads the remap configuration again. A metric that no rule matches counts as
  after the last rule.
* A metric with the same label names in another order keeps the value of each
  label.
* A metric with other label names, but the same number of labels, takes the
  label names of the family by position.
* The output leaves out a metric with another number of labels.
* The samples of a family are all string metrics from ``strings.names``, or
  all other metrics. The metric that sets the label names of the family
  decides which. When a metric from ``strings.names`` and another metric have
  the same rule, or both match no rule, the other metric sets the label names.
  The output leaves out a metric of the other kind.
* When two metrics have the same labels, the output has only the first one
  that the plugin sees. The label ``strings.label`` is not a part of this
  comparison, because its string is the value of the metric. Two metrics from
  ``strings.names`` with the same other labels are therefore duplicates,
  whatever their strings.

For each family and each of these cases, the plugin logs a warning the first
time that it leaves out, relabels or retypes a metric, or finds two metrics
with the same labels. The metrics in `Plugin Metrics`_ count each such sample.

Plugin Metrics
==============

The plugin counts its work in these metrics, which Traffic Server keeps for the
whole process, for the global plugin and all remap rules together:

``plugin.stats_over_http.requests``
   Requests for the statistics that the plugin answered, ``503`` responses
   included.

``plugin.stats_over_http.renders``
   Renders of the statistics.

``plugin.stats_over_http.render_us``
   The CPU time of the renders, in microseconds, on the task threads.

``plugin.stats_over_http.intercept_us``
   The CPU time, in microseconds, that the plugin uses on the event threads to
   send the responses. It does not include the time that Traffic Server uses to
   write the responses to the clients.

``plugin.stats_over_http.bytes_out``
   The bytes of the responses, headers included.

``plugin.stats_over_http.series``
   The samples that the plugin wrote in Prometheus renders.

``plugin.stats_over_http.series_dropped``
   The samples that the plugin left out because of their labels, their kind,
   an invalid family name or ``limits.max_series``.

``plugin.stats_over_http.series_relabeled``
   The samples that the plugin wrote with the label names of their family.

``plugin.stats_over_http.series_duplicates``
   The samples that the plugin left out because another metric has the same
   series.

``plugin.stats_over_http.series_type_conflicts``
   The samples that the plugin wrote with the type of their family instead of
   their own.

See `Prometheus Rules`_.

``plugin.stats_over_http.waiter_timeouts``
   Requests that got a ``503`` because no render finished within
   :option:`--wait-timeout-ms`.

``plugin.stats_over_http.config_errors``
   Loads of a remap rule whose configuration file was missing or had an error.
   Each remap reload that finds the error counts again.
