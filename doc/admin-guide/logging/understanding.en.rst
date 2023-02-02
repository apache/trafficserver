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

.. _admin-logging-understanding:

Understanding |TS| Logs
***********************

|TS| records information about every transaction (or request) it processes and
every error it detects in log files. This information is separated into various
logs, which are discussed below.

By analyzing the log files, you can determine how many people use the |TS|
cache, how much information each person requested, what pages are most popular,
and so on. You can analyze the standard format log files with off-the-shelf
analysis packages. To help with log file analysis, you can separate log files
so they contain information specific to protocol or hosts. You can also
configure |TS| to roll log files automatically at specific intervals during the
day or when they reach a certain size.

Enabling Logs
=============

By default, |TS| creates both error and event log files and
records system information in system log files. You can disable event
logging and/or error logging by setting the configuration variable
:ts:cv:`proxy.config.log.logging_enabled` in :file:`records.yaml`
to one of the following values:

===== =========================================================================
Value Description
===== =========================================================================
``0`` Disable both event and error logging.
``1`` Enable error logging only.
``2`` Enable event logging only.
``3`` Enable both event and error logging.
===== =========================================================================

.. _admin logging-types:

Log Types
=========

Three separate classes of log files exist: `System Logs`_, `Error Logs`_, and
`Event Logs`_. The fourth log type covered here, `Summary Logs`_ are a special
instance of the event logs, but instead of including details of individual
transactions, the summary logs allow you to emit log entries which aggregate
all events that occur over arbitrary periods of time (the specific period of
time being a fixed configuration of each summary log you create).

.. _admin-logging-type-system:

System Logs
-----------

System log files record system information, including messages about the state
of |TS| and any errors or warnings it produces.  This kind of information might
include a note that event log files were rolled or an error indicating that |TS|
was restarted. If |TS| is failing to start properly on your system(s), this is
the first place you'll want to look for possible hints as to the cause.

All system information messages are logged with the system-wide logging
facility :manpage:`syslog` under the daemon facility. The
:manpage:`syslog.conf(5)` configuration file (stored in the ``/etc`` directory)
specifies where these messages are logged. A typical location is
``/var/log/messages`` (Linux).

The :manpage:`syslog(8)` process works on a system-wide basis, so it serves as
the single repository for messages from |TS| process (:program:`traffic_server`).

System information logs observe a static format. Each log entry in the log
contains information about the date and time the error was logged, the hostname
of the |TS| that reported the error, and a description of the error or warning.

.. _admin-logging-type-error:

Error Logs
----------

Error log files record information about why a particular transaction was in
error. Refer to :ref:`admin-monitoring-errors` for a list of the messages
logged by |TS|.

.. _admin-logging-type-event:

Event Logs
----------

Event log files (also called access logs) record information about the state of
each transaction |TS| processes and form the true bulk of logging output in
|TS| installations. Most of the remaining documentation in this chapter applies
to creating, formatting, rotating, and filtering event logs.

Individual event log outputs are configured in :file:`logging.yaml` and as
such, the documentation provided in that configuration file's section should be
consulted in concert with the sections of this chapter.

.. _admin-logging-type-summary:

Summary Logs
------------

Summary logs are an extension of the event logs, but instead of providing
details for individual events, aggregate statistics are presented for all
events occurring within the specified time window. Summary logs have access to
all of the same fields as event logs, with the restriction that every field
must be used within an aggregating function. Summary logs may not mix both
aggregated and unaggregated fields.

The aggregating functions available are:

=========== ===================================================================
Function    Description
=========== ===================================================================
``AVG``     Average (mean) of the given field's value from all events within
            the interval. May only be used on numeric fields.
``COUNT``   The total count of events which occurred within the interval. No
            field name is necessary (``COUNT(*)`` may be used instead).
``FIRST``   The value of the first event, chronologically, which was observed
            within the interval. May be used with any type of field; numeric or
            otherwise.
``LAST``    The value of the last event, chronologically, which was observed
            within the interval. May be used with any type of field; numeric or
            otherwise.
``SUM``     Sum of the given field's value from all events within the interval.
            May only be used on numeric fields.
=========== ===================================================================

Summary logs are defined in :file:`logging.yaml` just like regular event
logs, with the only two differences being the exclusive use of the
aforementioned aggregate functions and the specification of an interval, as so:

.. code:: yaml

   formats:
   - name: mysummary
     format: '%<operator(field)> , %<operator(field)>'
     interval: n

The interval itself is given with *n* as the number of seconds for each period
of aggregation. There is no default value.

Logging Performance
-------------------

In normal operations, log entries are strictly ordered in the output file.
This serialization of entries comes at a cost as multiple threads potentially
contend for log access.  For binary logs or when order does not matter, ATS supports
faster logging where each thread can buffer its own entries.  In this mode, log
parsers will need to expect out of order entries, but ATS can log much larger
transaction rates. Consider adding fields to the log format that include the timestamp so
entries can be reordered if necessary (see :ref:`admin-logging-fields-time`)

.. code:: yaml

   formats:
   - name: mysummary
     format: '%<operator(field)> , %<operator(field)>'
     fast: true
