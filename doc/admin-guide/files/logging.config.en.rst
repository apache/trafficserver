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

.. configfile:: logging.config

logging.config
**************

The :file:`logging.config` file defines all custom log file formats, filters,
and processing options. The file itself is a Lua script.

.. important::

   This configuration file replaces the XML based logs_xml.config from past
   |TS| releases. If you are upgrading from a |TS| release which used that
   configuration file, and you have created custom log formats, filters, and
   destinations, you will need to update those settings to this format.

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
may be defined as a reusable variable in your :file:`logging.config` for ease
of reference, particularly when you may have more than one log using the same
format. Which approach you use is entirely up to you, though it's strongly
recommended to create an explicit format object if you intend to reuse the same
format for multiple log files.

To create a format object, store the result of the ``format`` function in a
variable. The function takes a table with two attributes: a mandatory string
``Format`` which defines the output format string for every event; and an
optional number ``Interval`` defining the aggregation interval for summary
logs.

.. code:: lua

   -- A one-line-per-event format that just prints event timestamps.
   myformat = format {
     Format = '%<cqtq>'
   }

   -- An aggregation/summary format that prints the last event timestamp from
   -- the interval along with the total count of events in the same interval.
   -- (Doing so every 30 seconds.)
   mysummaryformat = format {
     Format = '%<LAST(cqtq)> %<COUNT(*)>',
     Interval = 30
   }

You may define as many and as varied a collection of format objects as you
desire.

Format Specification
~~~~~~~~~~~~~~~~~~~~

The format specification provided as the required ``Format`` entry of the table
passed to the format function is a simple string, containing whatever mixture
of logging field variables and literal characters meet your needs. Logging
fields are discussed in great detail in the :ref:`custom-logging-fields`
section.

Flexible enough to not only emulate the logging formats of most other proxy and
HTTP servers, but also to provide even finer detail than many of them, the
logging fields are very easy to use. Within the format string, logging fields
are indicated by enclosing their name within angle brackets (``<`` and ``>``),
preceded by a percent symbol (``%``). For example, returning to the altogether
too simple format shown earlier, the following format string::

    '%<cqtq>'

Defines a format in which nothing but the value of the logging field
:ref:`cqtq` is interpolated for each event's entry in the log. We could include
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
:ref:`custom-logging-fields`.

Aggregation Interval
~~~~~~~~~~~~~~~~~~~~

Every format may be given an optional ``Interval`` value, specified as the
number of seconds over which events destined for a log using the format are
aggregated and summarized. Logs which use formats containing an aggregation
interval do not behave like regular logs, with a single line for every event.
Instead, they emit a single line only every *interval*\ -seconds.

These types of logs are described in more detail in
:ref:`admin-monitoring-logging-summary-logs`.

Formats have no interval by default, and will generate event-based logs unless
given one.

.. _admin-custom-logs-filters:

Filters
-------

Filters may be used, optionally, to accept or reject logging for matching
events, or to scrub the values of individual fields from logging output (while
retaining other information; useful for ensuring that sensitive information
cannot inadvertently make it into log files).

Filter objects are created by calling one of the following functions:

filter.accept(string)
    Creates a filter object which accepts events for logging which match the
    rule specified in ``string``.

filter.reject(string)
    Creates a filter object which rejects events for logging which match the
    rule specified in ``string``.

filter.wipe(string)
    Creates a filter object which clears the values of query parameters listed
    in ``string``.

For both ``accept`` and ``wipe`` filters, the string passed defines a rule in
the following format::

    <field> <operator> <value>

Filter Fields
~~~~~~~~~~~~~

The log fields have already been discussed in the `Formats`_ section above. For
a reference to the available log field names, see :ref:`custom-logging-fields`.
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
name field will be compared. For integer matches, all of the operators are
effectively equivalent and require the field to be equal to the given integer.
For IP addresses, ranges may be specified by separating the first address and
the last of the range with a single ``-`` dash, as ``10.0.0.0-10.255.255.255``
which gives the ranges for the 10/8 network. Other network notations are not
supported at this time.

Wiping Filters
~~~~~~~~~~~~~~

Filters created with ``filter.wipe`` function differently than the accept and
reject filters. Instead of a rule, as described above for those filter types,
the wiping filter simply lists the query parameter(s) whose values should be
scrubbed before any logging occurs. This prevents sensitive information from
being logged by fields which include the query string portion of the request
URL. It can also be useful to remove things like cache-busting or
inconsequentially variable parameters that might otherwise obfuscate the
reporting from log analyzers.

Multiple query parameters may be listed, separated by spaces, though only the
first occurence of each will be wiped from the query string if any individual
parameter appears more than once in the URL.

.. _admin-custom-logs-logs:

Logs
----

Up to this point, we've only described what events should be logged and what
they should look like in the logging output. Now we define where those logs
should be sent. Three options currently exist for the type of logging output,
and each is selected by invoking the appropriate function. All three functions
take a single Lua table as their argument, with the same set of key/value
pairs.

log.ascii(table)
    Creates an ASCII logging object.

log.binary(table)
    Creates a binaryy logging object.

log.pipe(table)
    Creates a logging object that logs to a pipe.

There is no need to capture the return values of these functions. Which type of
logging output you choose depends largely on how you intend to process the logs
with other tools, and a discussion of the merits of each is covered elsewhere,
in :ref:`admin-logging-binary-v-ascii`.

The following subsections cover the contents of the table which should be
passed when creating your logging object. Only ``Filename`` and ``Format`` are
required.

=================== =========== ===============================================
Name                Type        Description
=================== =========== ===============================================
Filename            string      The name of the logfile relative to the default
                                logging directory (set with
                                :ts:cv:`proxy.config.log.logfile_dir`).
Format              string or   Either a format object created earlier, or a
                    format obj  string with a valid format specification.
Header              string      If present, emitted as the first line of each
                                new log file.
RollingEnabled      *see below* Determines the type of log rolling to use (or
                                whether to disable rolling). Overrides
                                :ts:cv:`proxy.config.log.rolling_enabled`.
RollingIntervalSec  number      Interval in seconds between log file rolling.
                                Overrides
                                :ts:cv:`proxy.config.log.rolling_interval_sec`.
RollingOffsetHr     number      Specifies an hour (from 0 to 23) at which log
                                rolling is guaranteed to align. Only has an
                                effect if RollingIntervalSec is set to greater
                                than one hour. Overrides
                                :ts:cv:`proxy.config.log.rolling_offset_hr`.
RollingSizeMb       number      Size, in megabytes, at which log files are
                                rolled.
Filters             array of    The optional list of filter objects which
                    filters     restrict the individual events logged.
CollationHosts      array of    If present, one or more strings specifying the
                    strings     log collation hosts to which logs should be
                                delivered, each in the form of "<ip>:<port>".
                                :ref:`admin-monitoring-logging-log-collation`
                                for more information.
=================== =========== ===============================================

Enabling log rolling may be done globally in :file:`records.config`, or on a
per-log basis by passing appropriate values for the ``RollingEnabled`` key. The
latter method may also be used to effect different rolling settings for
individual logs. The numeric values that may be passed are the same as used by
:ts:cv:`proxy.config.log.rolling_enabled`. For convenience and readability,
the following predefined variables may also be used in :file:`logging.config`:

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

.. code:: lua

   minimalfmt = format {
     Format = '%<chi> : %<cqu> : %<pssc>'
   }

The following is an example of a format that uses aggregate operators to
produce a summary log:

.. code:: lua

   summaryfmt = format {
     Format = '%<LAST(cqts)> : %<COUNT(*)> : %<SUM(psql)>',
     Interval = 10
   }

The following is an example of a filter that will cause only REFRESH_HIT events
to be logged:

.. code:: lua

   refreshhitfilter = filter.accept('pssc MATCH REFRESH_HIT')

The following is an example of a filter that will cause the value of the first
query parameter named ``passwd`` to be wiped.

.. code:: lua

   passwdfilter = filter.wipe('passwd')

The following is an example of a log specification that creates a local log
file for the minimal format defined earlier. The log filename will be
``minimal.log`` because we select the ASCII logging format.

.. code:: lua

   log.ascii {
     Filename = 'minimal',
     Format = minimalfmt
   }

The following is an example of a log specification that creates a local log
file using the summary format from earlier, and only includes events that
matched the REFRESH_HIT filter we created.

.. code:: lua

   log.ascii {
     Filename = 'refreshhit_summary',
     Format = summaryfmt,
     Filters = { refreshhitfilter }
   }

Further Reading
===============

As the :file:`logging.config` configuration file is just a Lua script (with a
handful of predefined functions and variables), general Lua references may be
handy for those not already familiar with the language.

* `Lua Documentation <https://www.lua.org/docs.html>`_
