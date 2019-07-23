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

.. configfile:: logging.yaml

logging.yaml
**************

The :file:`logging.yaml` file defines all custom log file formats, filters,
and processing options.

.. important::

   This configuration file replaces the XML based logs_xml.config, as well as
   the Lua based logging.config from past |TS| releases. If you are upgrading
   from a |TS| release which used either the XML or the Lua configuration file
   format, and you have created custom log formats, filters, and destinations,
   you will need to update those settings to this format.

.. _admin-custom-logs:

Log Definitions
===============

Custom logs are configured by the combination of three key elements: a format,
an optional filter, and a log destination.

A :ref:`format <admin-custom-logs-formats>` defines how log lines will appear
(as well as whether the logs using the format will be event logs or summary
logs).

A :ref:`filter <admin-custom-logs-filters>` defines what events do, and what
events don't, make it into the logs employing the filter.

A :ref:`log <admin-custom-logs-logs>` defines where the record of events or
summaries ends up.

.. _admin-custom-logs-formats:

Formats
-------

Custom logging formats may be provided directly to a log definition, or they
may be defined as a reusable variable in your :file:`logging.yaml` for ease
of reference, particularly when you may have more than one log using the same
format. Which approach you use is entirely up to you, though it's strongly
recommended to create an explicit format object if you intend to reuse the same
format for multiple log files.

Custom formats are defined by choosing a ``name`` to identify the given logging
format, and a ``format`` string, which defines the output format string for
every event. An optional ``interval`` attribute can be specified to define the
aggregation interval for summary logs.

.. code:: yaml

   # A one-line-per-event format that just prints event timestamps.
   formats:
   - name: myformat
     format: '%<cqtq>'

   # An aggregation/summary format that prints the last event timestamp from
   # the interval along with the total count of events in the same interval.
   # (Doing so every 30 seconds.)
   formats:
   - name: mysummaryformat
     format: '%<LAST(cqtq)> %<COUNT(*)>'
     interval: 30

You may define as many and as varied a collection of format objects as you
desire.

Format Specification
~~~~~~~~~~~~~~~~~~~~

The format specification provided as the required ``format`` attribute of the
objects listed in ``formats`` is a simple string, containing whatever mixture
of logging field variables and literal characters meet your needs. Logging
fields are discussed in great detail in the :ref:`admin-logging-fields`
section.

Flexible enough to not only emulate the logging formats of most other proxy and
HTTP servers, but also to provide even finer detail than many of them, the
logging fields are very easy to use. Within the format string, logging fields
are indicated by enclosing their name within angle brackets (``<`` and ``>``),
preceded by a percent symbol (``%``). For example, returning to the altogether
too simple format shown earlier, the following format string::

    '%<cqtq>'

Defines a format in which nothing but the value of the logging field :ref:`cqtq
<cqtq>` is interpolated for each event's entry in the log. We could include
some literal characters in the log output by updating the format specification
as so::

    'Event received at %<cqtq>'

Because the string "Event received at " (including the trailing space) is just
a bunch of characters, not enclosed in ``%<...>``, it is repeated verbatim in
the logging output.

Multiple logging fields may of course be used::

    '%<cqtq> %<chi> %<cqhm> %<cqtx>'

Each logging field is separately enclosed in its own percent-brace set.

There are a small number of logging fields which extend this simple format,
primarily those dealing with request and response headers. Instead of defining
a separate logging field name for every single possible HTTP header (an
impossible task, given that arbitrary vendor/application headers may be present
in both requests and responses), there are instead single logging fields for
each of the major stages of an event lifecycle that permit access to named
headers, such as::

    '%<{User-Agent}cqh>'

Which emits to the log the value of the client request's ``User-Agent`` HTTP
header. Other stages of the event lifecycle have similar logging fields:
``pqh`` (proxy requests), ``ssh`` (origin server responses), and ``psh``
(proxy responses).

You will find a complete listing of the available fields in
:ref:`admin-logging-fields`.

Aggregation Interval
~~~~~~~~~~~~~~~~~~~~

Every format may be given an optional ``interval`` value, specified as the
number of seconds over which events destined for a log using the format are
aggregated and summarized. Logs which use formats containing an aggregation
interval do not behave like regular logs, with a single line for every event.
Instead, they emit a single line only every *interval*\ -seconds.

These types of logs are described in more detail in
:ref:`admin-logging-type-summary`.

Formats have no interval by default, and will generate event-based logs unless
given one.

.. _admin-custom-logs-filters:

Filters
-------

Trafficserver supports different type of filters : ``accept``, ``reject`` and ``wipe_field_value``.
They may be used, optionally, to accept, reject logging or mask query param values for matching events.

Filter objects are created by assigning them a ``name`` to be used later to
refer to the filter, as well as an ``action`` (either ``accept``, ``reject`` or
``wipe_field_value``). ``Accept``, ``reject`` or ``wipe_field_value`` filters require
a ``condition`` against which to match all events. The ``condition`` fields must
be in the following format::

    <field> <operator> <value>

For example, the following snippet defines a filter that matches all POST
requests:

.. code:: yaml

   filters:
   - name: postfilter
     action: accept
     condition: cqhm MATCH POST

Filter Fields
~~~~~~~~~~~~~

The log fields have already been discussed in the `Formats`_ section above. For
a reference to the available log field names, see :ref:`admin-logging-fields`.
Unlike with the log format specification, you do not wrap the log field names
in any additional markup.

Filter Operators
~~~~~~~~~~~~~~~~

The operators describe how to perform the matching in the filter rule, and may
be any one of the following:

``MATCH``
    True if the values of ``field`` and ``value`` are identical.
    Case-sensitive.

``CASE_INSENSITIVE_MATCH``
    True if the values of ``field`` and ``value`` are identical.
    Case-insensitive.

``CONTAIN``
    True if the value of ``field`` contains ``value`` (i.e. ``value`` is a
    substring of the contents of ``field``). Case-sensitive.

``CASE_INSENSITIVE_CONTAIN``
    True if the value of ``field`` contains ``value`` (i.e. ``value`` is a
    substring of the contents of ``field``). Case-insensitive.

Filter Values
~~~~~~~~~~~~~

The final component of a filter string specifies the value against which the
name field will be compared.

For integer matches, all of the operators are effectively equivalent and require
the field to be equal to the given integer. If you wish to match multiple
integers, provide a comma separated list like this::

    <field> <operator> 4,5,6,7

String matches work similarly to integer matches. Multiple matches are also
supported via a comma separated list. For example::

    <field> <operator> e1host,host2,hostz

For IP addresses, ranges may be specified by separating the first address and
the last of the range with a single ``-`` dash, as ``10.0.0.0-10.255.255.255``
which gives the ranges for the 10/8 network. Other network notations are not
supported at this time.

.. note::

    It may be tempting to attach multiple Filters to a log object
    reject multiple log fields (in lieu of providing a single comma separated list
    to a single Filter). Avoid this temptation and use a comma separated list of reject objects
    instead. Remember that you may not have multiple accept filter objects.
    Attaching multiple filters does the opposite of what you'd
    expect. If, for example, we had 2 accept log filters, each disjoint from the other,
    nothing will ever get logged on the given log object.


.. _admin-custom-logs-logs:

Logs
----

Up to this point, we've only described what events should be logged and what
they should look like in the logging output. Now we define where those logs
should be sent.

Three options currently exist for the type of logging output: ``ascii``,
``binary``, and ``ascii_pipe``.  Which type of logging output you choose
depends largely on how you intend to process the logs with other tools, and a
discussion of the merits of each is covered elsewhere, in
:ref:`admin-logging-ascii-v-binary`.

The following subsections cover the attributes you should specify when creating
your logging object. Only ``filename`` and ``format`` are required.

====================== =========== =================================================
Name                   Type        Description
====================== =========== =================================================
filename               string      The name of the logfile relative to the default
                                   logging directory (set with
                                   :ts:cv:`proxy.config.log.logfile_dir`).
format                 string      a string with a valid named format specification.
header                 string      If present, emitted as the first line of each
                                   new log file.
rolling_enabled        *see below* Determines the type of log rolling to use (or
                                   whether to disable rolling). Overrides
                                   :ts:cv:`proxy.config.log.rolling_enabled`.
rolling_interval_sec   number      Interval in seconds between log file rolling.
                                   Overrides
                                   :ts:cv:`proxy.config.log.rolling_interval_sec`.
rolling_offset_hr      number      Specifies an hour (from 0 to 23) at which log
                                   rolling is guaranteed to align. Only has an
                                   effect if RollingIntervalSec is set to greater
                                   than one hour. Overrides
                                   :ts:cv:`proxy.config.log.rolling_offset_hr`.
rolling_size_mb        number      Size, in megabytes, at which log files are
                                   rolled.
rolling_min_count      number      Specifies the minimum number of rolled logs to
                                   keep.
filters                array of    The optional list of filter objects which
                       filters     restrict the individual events logged. The array
                                   may only contain one accept filter.
====================== =========== =================================================

Enabling log rolling may be done globally in :file:`records.config`, or on a
per-log basis by passing appropriate values for the ``rolling_enabled`` key. The
latter method may also be used to effect different rolling settings for
individual logs. The numeric values that may be passed are the same as used by
:ts:cv:`proxy.config.log.rolling_enabled`. For convenience and readability,
the following predefined variables may also be used in :file:`logging.yaml`:

log.roll.none
    Disable log rolling.

log.roll.time
    Roll at a certain time frequency, specified by RollingIntervalSec and
    RollingOffsetHr.

log.roll.size
    Roll when the size exceeds RollingSizeMb.

log.roll.both
    Roll when either the specified rolling time is reached or the specified
    file size is reached.

log.roll.any
    Roll the log file when the specified rolling time is reached if the size of
    the file equals or exceeds the specified size.

Examples
========

The following is an example of a format that collects information using three
common fields:

.. code:: yaml

   formats:
   - name: minimalfmt
     format: '%<chi> : %<cqu> : %<pssc>'

The following is an example of a format that uses aggregate operators to
produce a summary log:

.. code:: yaml

   formats:
   - name: summaryfmt
     format: '%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>'
     interval: 10

The following is an example of a filter that will cause only REFRESH_HIT events
to be logged:

.. code:: yaml

   filters:
   - name: refreshhitfilter
     action: accept
     condition: pssc MATCH REFRESH_HIT

The following is an example of a log specification that creates a local log
file for the minimal format defined earlier. The log filename will be
``minimal.log`` because we select the ASCII logging format.

.. code:: yaml

   logs:
   - mode: ascii
     filename: minimal
     format: minimalfmt

The following is an example of a log specification that creates a local log
file using the summary format from earlier, and only includes events that
matched the REFRESH_HIT filter we created.

.. code:: yaml

   logs:
   - mode: ascii
     filename: refreshhit_summary
     format: summaryfmt
     filters:
     - refreshhitfilter
